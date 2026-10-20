#include "DynamicTexture.h"

#include <abc/Console.h>

#include "../AbcRenderInterface.h"
#include "../RmlUiModule.h"
#include "../VGUI2ExtensionImport.h"

namespace services::texture {

DynamicTexture::DynamicTexture(std::string prefix, rmlui::TextureBlend blend)
    : Blend(blend), Prefix(std::move(prefix)), Uri() {
  Uri = Prefix + "0";
}

DynamicTexture::~DynamicTexture() {
  release();
}

void DynamicTexture::release() {
  if (auto *Interface = rmlui::renderInterface())
    Interface->unregisterExternalTexture(Prefix);
  if (TextureId != 0) {
    vgui2ext::deleteTextureById(TextureId);
    TextureId = 0;
  }
  Width = Height = 0;
  Uri = Prefix + "0";
}

bool DynamicTexture::upload(const unsigned char *rgba, int width, int height) {
  if (!rgba || width <= 0 || height <= 0)
    return false;
  if (TextureId == 0 || width != Width || height != Height) {
    if (auto *Interface = rmlui::renderInterface())
      Interface->unregisterExternalTexture(Prefix);
    if (TextureId != 0) {
      vgui2ext::deleteTextureById(TextureId);
      TextureId = 0;
    }
    const int Id = vgui2ext::createTexture();
    if (Id == 0) {
      static bool Warned = false;
      if (!Warned) {
        abc::ConsoleWarn("[AbcUi] surface texture channel unavailable; dynamic "
                         "textures disabled\n");
        Warned = true;
      }
      return false;
    }
    TextureId = Id;
    Width = width;
    Height = height;
    ++Generation;
    Uri = Prefix + std::to_string(Generation);
  }
  vgui2ext::uploadTexture(TextureId, rgba, width, height);
  if (auto *Interface = rmlui::renderInterface())
    Interface->registerExternalTexture(
        Prefix, static_cast<Rml::TextureHandle>(TextureId), Width, Height,
        nullptr, Blend);
  return true;
}

} // namespace services::texture
