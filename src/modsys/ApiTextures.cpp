#include "ApiTextures.h"

#include "ModuleResources.h"
#include "ModuleVfs.h"
#include "framework/ImageLoader.h"
#include "framework/ResourceLimits.h"
#include "framework/TextureSink.h"
#include "render/RadarRender.h"
#include "rmlui/AbcRenderInterface.h"
#include "rmlui/RmlUiModule.h"

#include <cstring>
#include <map>
#include <string>

namespace framework::TexturesApi {
namespace {
struct Texture {
  int id = 0;
  std::string uri;
  module_resources::Registration lifetime;
};

std::map<AbcUiTexture, Texture> &textures() {
  static std::map<AbcUiTexture, Texture> instance;
  return instance;
}

void remove(AbcUiTexture handle) {
  const auto it = textures().find(handle);
  if (it == textures().end())
    return;
  auto texture = textures().extract(it);
  radarrender::forgetMask(texture.mapped().id);
  if (auto *renderer = rmlui::renderInterface()) {
    renderer->endFrame();
    renderer->unregisterExternalTexture(texture.mapped().uri);
  }
  framework::texture::release(texture.mapped().id);
}

bool owned(AbcUiTexture handle) {
  const auto it = textures().find(handle);
  return it != textures().end() &&
         it->second.lifetime.owner() == module_resources::current();
}

int32_t upload(const void *rgba, uint32_t bytes, int32_t width, int32_t height,
               int32_t additive, AbcUiTexture *out) {
  if (out)
    *out = 0;
  size_t required = 0;
  if (!out || !rgba || !ResourceLimits::imageBytes(width, height, required) ||
      required != bytes)
    return ABCUI_ERROR_INVALID;
  const auto owner = module_resources::current();
  if (!owner || !module_resources::accepting())
    return ABCUI_ERROR_FORBIDDEN;
  auto *renderer = rmlui::renderInterface();
  if (!renderer || !module_resources::contextRegistrationsOpen() ||
      !framework::texture::available())
    return ABCUI_ERROR_UNAVAILABLE;
  if (textures().size() >= 1024)
    return ABCUI_ERROR_LIMIT;
  const int id = framework::texture::create();
  if (!id)
    return ABCUI_ERROR_UPLOAD;
  framework::texture::upload(id, static_cast<const unsigned char *>(rgba),
                             width, height);
  static AbcUiTexture next = 1;
  const auto handle = next++;
  Texture record;
  record.id = id;
  record.uri = "moduletex://" + owner->id + "/" + std::to_string(handle) + "/";
  renderer->registerExternalTexture(
      record.uri, static_cast<Rml::TextureHandle>(id), width, height, nullptr,
      additive ? rmlui::TextureBlend::Additive
               : rmlui::TextureBlend::StraightAlpha);
  record.lifetime.track([handle] { remove(handle); },
                        module_resources::Lifetime::Context);
  textures().emplace(handle, std::move(record));
  *out = handle;
  return ABCUI_OK;
}

int32_t load(const char *uri, int32_t additive, AbcUiTexture *out) {
  if (out)
    *out = 0;
  if (!uri || !out)
    return ABCUI_ERROR_INVALID;
  const auto owner = module_resources::current();
  if (!owner || !ModuleVfs::canRead(owner->id, uri))
    return ABCUI_ERROR_FORBIDDEN;
  PackageEntry entry;
  if (!ModuleVfs::open(uri, entry, nullptr))
    return ABCUI_ERROR_NOT_FOUND;
  ImageLoader::DecodedImage image;
  if (!ImageLoader::loadFromMemory(
          {static_cast<const std::byte *>(entry.data), entry.size}, image))
    return ABCUI_ERROR_DECODE;
  return upload(image.Pixels.data(), static_cast<uint32_t>(image.Pixels.size()),
                image.Width, image.Height, additive, out);
}

int32_t uri(AbcUiTexture handle, char *out, uint32_t size) {
  if (!owned(handle))
    return ABCUI_ERROR_INVALID;
  const auto &value = textures().at(handle).uri;
  if (!out || size <= value.size())
    return ABCUI_ERROR_LIMIT;
  std::memcpy(out, value.c_str(), value.size() + 1);
  return ABCUI_OK;
}

void release(AbcUiTexture handle) {
  if (owned(handle))
    remove(handle);
}
} // namespace

int textureId(AbcUiTexture handle) {
  return owned(handle) ? textures().at(handle).id : 0;
}

const AbcUiTexturesApi *get() {
  static const AbcUiTexturesApi api{sizeof(api), upload, load, uri, release};
  return &api;
}
} // namespace framework::TexturesApi
