// rmlui::AbcRenderInterface implementation - see AbcRenderInterface.h for the
// semantics (blend conversion, texture ids, viewport-as-scissor R-1 verdict).
#include <metahook.h>

#include "AbcRenderInterface.h"
#include "framework/PreviewImage.h"
#include <abc/Plugin.h>
#include "../framework/ResourceLimits.h"

#include <cstddef>
#include <cstring>

#include <abc/Console.h>

#include "texture/Avatars.h"
#include "texture/SpriteResource.h"
#include "../framework/ImageLoader.h"
#include "../modsys/ModuleVfs.h"
#include "../render/BackendHost.h"
#include "../render/IRenderBackend.h"
#include "VGUI2ExtensionImport.h"

namespace rmlui {

namespace {

// Upload row order of the VGUI2 surface DrawSetTextureRGBA channel, settled
// by the Phase 3 in-game spike: false = uploads land top-scanline-first at
// v=0, which matches RmlUi's (0,0)=top-left UV convention without extra
// flipping (the image itself is flipped out of FreeImage's bottom-up order
// in LoadTexture below).
constexpr bool kFlipTextureUpload = false;

void ToStraightColor(const Rml::ColourbPremultiplied &in, float out[4]) {
  const float a = in.alpha / 255.0f;
  out[3] = a;
  if (a > 0.0f) {
    out[0] = (in.red / 255.0f) / a;
    out[1] = (in.green / 255.0f) / a;
    out[2] = (in.blue / 255.0f) / a;
  } else {
    out[0] = out[1] = out[2] = 0.0f;
  }
}

void UnpremultiplyInPlace(unsigned char *rgba, size_t pixelCount) {
  for (size_t i = 0; i < pixelCount; i++) {
    unsigned char *px = rgba + i * 4;
    const unsigned int a = px[3];
    if (a < 255) {
      px[0] = static_cast<unsigned char>(a > 0 ? (px[0] * 255u) / a : 0);
      px[1] = static_cast<unsigned char>(a > 0 ? (px[1] * 255u) / a : 0);
      px[2] = static_cast<unsigned char>(a > 0 ? (px[2] * 255u) / a : 0);
    }
  }
}

void FlipRows(unsigned char *pixels, int width, int height) {
  const int stride = width * 4;
  std::vector<unsigned char> row(stride);
  for (int y = 0; y < height / 2; y++) {
    unsigned char *a = pixels + static_cast<size_t>(y) * stride;
    unsigned char *b = pixels + static_cast<size_t>(height - 1 - y) * stride;
    memcpy(row.data(), a, stride);
    memcpy(a, b, stride);
    memcpy(b, row.data(), stride);
  }
}

} // namespace

AbcRenderInterface::~AbcRenderInterface() {
  endFrame();
  for (const auto &[handle, record] : Textures)
    if (record.Owned)
      vgui2ext::deleteTextureById(record.Id);
}

void AbcRenderInterface::beginFrame() {
  // Frame brackets start with no 2D state installed; the first geometry
  // issues Set2D. (The framebuffer bind is done by RmlUiModule.)
}

void AbcRenderInterface::endFrame() {
  if (TwoDActive) {
    if (abc::render::IRenderBackend *backend = abc::render::backend())
      backend->finish2D();
    TwoDActive = false;
    AppliedScissor = false;
    AppliedScissorRect = Rml::Rectanglei::MakeInvalid();
  }
}

Rml::CompiledGeometryHandle
AbcRenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                    Rml::Span<const int> indices) {
  // Baked shapes use the backend contract's vertex POD; the backend DLL
  // pins its own layout against its native types (R-14 lesson: mirror
  // layouts are pinned where they are consumed).
  if (vertices.empty() || indices.empty())
    return {};

  auto geometry = std::make_unique<CompiledGeometry>();
  // One-time bake (05 §8): colours un-premultiplied, indices widened -
  // RenderGeometry never repeats this work.
  geometry->Textured.resize(vertices.size());
  for (size_t i = 0; i < vertices.size(); i++) {
    const Rml::Vertex &v = vertices[i];
    float *position = geometry->Textured[i].position;
    float *texcoord = geometry->Textured[i].texcoord;
    position[0] = v.position.x;
    position[1] = v.position.y;
    texcoord[0] = v.tex_coord.x;
    texcoord[1] = v.tex_coord.y;
    ToStraightColor(v.colour, geometry->Textured[i].color);
  }
  geometry->Indices.reserve(indices.size());
  for (const int index : indices)
    geometry->Indices.push_back(static_cast<uint32_t>(index));
  const auto handle =
      reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.get());
  Geometries.emplace(handle, std::move(geometry));
  return handle;
}

void AbcRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry,
                                        Rml::Vector2f translation,
                                        Rml::TextureHandle texture) {
  abc::render::IRenderBackend *backend = abc::render::backend();
  if (!backend || geometry == 0)
    return;

  const auto *source = reinterpret_cast<const CompiledGeometry *>(geometry);
  // The lazy filled bake below mutates the cache inside the otherwise
  // read-only compiled record.
  auto *baked = reinterpret_cast<CompiledGeometry *>(geometry);
  const size_t vertexCount = source->Textured.size();
  const size_t indexCount = source->Indices.size();
  if (vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0)
    return;

  // With scissor active Rml still sends geometry overlapping the region;
  // Set2DEx confines rasterization to the region rect.
  // An empty region (zero width/height - e.g. a fully shrunken overflow
  // clip) must draw NOTHING: falling through to the unclipped path would
  // leak the clipped content (the armor-bar-at-zero-width bug).
  if (ScissorEnabled) {
    if (!ScissorRect.Valid() || ScissorRect.Width() <= 0 ||
        ScissorRect.Height() <= 0)
      return;
  }
  const bool scissor = ScissorEnabled;

  // Per-state 2D setup (translation stack balanced by endFrame).
  if (!TwoDActive || AppliedScissor != scissor ||
      (scissor && !(AppliedScissorRect == ScissorRect))) {
    endFrame();
    if (scissor) {
      // set2D takes GL window coords (bottom-left origin); Rml rects are
      // top-left origin, hence the y flip around screen height.
      const int screenH = backend->surfaceHeight();
      backend->set2D(ScissorRect.Left(), screenH - ScissorRect.Bottom(),
                     ScissorRect.Width(), ScissorRect.Height());
    } else {
      backend->set2D(0, 0, backend->surfaceWidth(), backend->surfaceHeight());
    }
    TwoDActive = true;
    AppliedScissor = scissor;
    AppliedScissorRect = ScissorRect;
  }

  // The common path's whole per-frame work: hand the translation (element
  // offset minus the set2D re-basing) to the backend's translation stack -
  // and draw straight from the baked arrays. Both parts are whole pixels
  // (RmlUi rounds its render translations; scissor rects are integer), so
  // this is exactly the old `applyTransform(v.position + translation)` under
  // an identity Rml transform, computed on the GPU.
  const float offsetX =
      translation.x - (scissor ? static_cast<float>(ScissorRect.Left()) : 0.0f);
  const float offsetY =
      translation.y - (scissor ? static_cast<float>(ScissorRect.Top()) : 0.0f);
  const bool translateOnGpu =
      !TransformActive && (offsetX != 0.0f || offsetY != 0.0f);
  if (translateOnGpu)
    backend->pushTranslation(offsetX, offsetY);

  const abc::render::Vertex *texturedVertices = source->Textured.data();
  const abc::render::FilledVertex *filledVertices = nullptr;
  const abc::render::Vertex *scratchTextured = nullptr;
  if (TransformActive) {
    // Rare path (CSS-transformed elements): M·(v + translation) - the
    // GL2 sample order: translate first, then the loaded matrix - minus
    // the scissor re-basing, per vertex, into reused scratch.
    const bool wantFilled = texture == 0;
    if (wantFilled) {
      ScratchFilled.resize(vertexCount);
      for (size_t i = 0; i < vertexCount; i++) {
        const Rml::Vector2f pos =
            applyTransform(Rml::Vector2f(source->Textured[i].position[0],
                                         source->Textured[i].position[1]) +
                           translation);
        ScratchFilled[i].position[0] =
            pos.x - (scissor ? static_cast<float>(ScissorRect.Left()) : 0.0f);
        ScratchFilled[i].position[1] =
            pos.y - (scissor ? static_cast<float>(ScissorRect.Top()) : 0.0f);
        ScratchFilled[i].color[0] = source->Textured[i].color[0];
        ScratchFilled[i].color[1] = source->Textured[i].color[1];
        ScratchFilled[i].color[2] = source->Textured[i].color[2];
        ScratchFilled[i].color[3] = source->Textured[i].color[3];
      }
      filledVertices = ScratchFilled.data();
    } else {
      ScratchTextured.resize(vertexCount);
      for (size_t i = 0; i < vertexCount; i++) {
        const abc::render::Vertex &v = source->Textured[i];
        const Rml::Vector2f pos = applyTransform(
            Rml::Vector2f(v.position[0], v.position[1]) + translation);
        abc::render::Vertex &out = ScratchTextured[i];
        out.position[0] =
            pos.x - (scissor ? static_cast<float>(ScissorRect.Left()) : 0.0f);
        out.position[1] =
            pos.y - (scissor ? static_cast<float>(ScissorRect.Top()) : 0.0f);
        out.texcoord[0] = v.texcoord[0];
        out.texcoord[1] = v.texcoord[1];
        out.color[0] = v.color[0];
        out.color[1] = v.color[1];
        out.color[2] = v.color[2];
        out.color[3] = v.color[3];
      }
      scratchTextured = ScratchTextured.data();
    }
  } else if (texture == 0) {
    // Untextured geometry: the filled-rect fast path (05 §3.2). The mesh
    // texture never changes after RmlUi built it, so the filled shape is
    // baked once and reused.
    if (baked->Filled.empty()) {
      baked->Filled.resize(vertexCount);
      for (size_t i = 0; i < vertexCount; i++) {
        baked->Filled[i].position[0] = source->Textured[i].position[0];
        baked->Filled[i].position[1] = source->Textured[i].position[1];
        baked->Filled[i].color[0] = source->Textured[i].color[0];
        baked->Filled[i].color[1] = source->Textured[i].color[1];
        baked->Filled[i].color[2] = source->Textured[i].color[2];
        baked->Filled[i].color[3] = source->Textured[i].color[3];
      }
    }
    filledVertices = baked->Filled.data();
  }

  // Texture handles are surface texture ids (int namespace).
  if (texture == 0) {
    abc::render::FilledDraw draw;
    draw.vertices = filledVertices;
    draw.vertexCount = vertexCount;
    draw.indices = source->Indices.data();
    draw.indexCount = indexCount;
    draw.mode = abc::render::Blend::StraightAlpha;
    draw.label = "AbcUi filled";
    backend->drawFilled(draw);
  } else {
    const auto record = Textures.find(texture);
    if (record == Textures.end()) {
      if (translateOnGpu)
        backend->popTranslation();
      return;
    }
    int textureId = record->second.Id;
    // External masked textures (radar map): the mask id rides the registered
    // slot, read live so a shape toggle needs no texture reload (the Rml
    // cache holds one entry for the URI).
    int maskTexture = 0;
    abc::render::Blend blend = AdditiveTextures.count(texture)
                                   ? abc::render::Blend::Additive
                                   : abc::render::Blend::StraightAlpha;
    if (!record->second.ExternalPrefix.empty()) {
      const auto live = ExternalTextures.find(record->second.ExternalPrefix);
      if (live == ExternalTextures.end()) {
        if (translateOnGpu)
          backend->popTranslation();
        return;
      }
      textureId = static_cast<int>(live->second.Texture);
      switch (live->second.Blend) {
      case TextureBlend::Additive:
        blend = abc::render::Blend::Additive;
        break;
      case TextureBlend::PremultipliedAlpha:
        blend = abc::render::Blend::PremultipliedAlpha;
        break;
      case TextureBlend::StraightAlpha:
        break;
      }
      if (live->second.MaskSlot && *live->second.MaskSlot)
        maskTexture = *live->second.MaskSlot;
    }
    const abc::render::Vertex *drawVertices =
        TransformActive ? scratchTextured : texturedVertices;
    abc::render::Draw draw;
    draw.vertices = drawVertices;
    draw.vertexCount = vertexCount;
    draw.indices = source->Indices.data();
    draw.indexCount = indexCount;
    draw.texture = textureId;
    draw.maskTexture = maskTexture;
    draw.mode = blend;
    draw.label = maskTexture ? "AbcUi masked" : "AbcUi";
    backend->drawTextured(draw);
  }

  if (translateOnGpu)
    backend->popTranslation();
}

void AbcRenderInterface::registerExternalTexture(const Rml::String &uriPrefix,
                                                 Rml::TextureHandle texture,
                                                 int width, int height,
                                                 const int *maskSlot,
                                                 TextureBlend blend) {
  if (uriPrefix.empty() || texture == 0)
    return;
  // A recycled engine id must not stay marked as owned: if a live record for
  // a file/font texture still carries the same id (possible after a
  // frame-buffer regen recycles the GL texture name), releasing that record
  // would delete a texture the external owner now controls.
  for (auto &entry : Textures) {
    TextureRecord &record = entry.second;
    if (record.Owned && record.Id == static_cast<int>(texture))
      record.Owned = false;
  }
  ExternalTexture entry;
  entry.Texture = texture;
  entry.Width = width;
  entry.Height = height;
  entry.MaskSlot = maskSlot;
  entry.Blend = blend;
  ExternalTextures[uriPrefix] = entry;
}

void AbcRenderInterface::unregisterExternalTexture(
    const Rml::String &uriPrefix) {
  ExternalTextures.erase(uriPrefix);
}

void AbcRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
  Geometries.erase(geometry);
}

Rml::TextureHandle AbcRenderInterface::LoadTexture(Rml::Vector2i &dimensions,
                                                   const Rml::String &source) {
  // Explicit native game content, never shared framework UI resources.
  if (source.rfind("engine://", 0) == 0)
    return loadFileTexture(dimensions, source.substr(9));
  if (source.rfind("preview://", 0) == 0) {
    framework::PreviewRequest Request;
    if (!framework::parsePreviewUri(source, Request) || !abc::g_pFileSystem)
      return 0;
    const auto File = abc::g_pFileSystem->Open(Request.Path.c_str(), "rb");
    if (!File)
      return 0;
    const auto Size = abc::g_pFileSystem->Size(File);
    std::vector<std::byte> Bytes;
    if (Size > 0 && Size <= framework::ResourceLimits::MaxFileBytes) {
      Bytes.resize(Size);
      if (abc::g_pFileSystem->Read(Bytes.data(), Size, File) !=
          static_cast<int>(Size))
        Bytes.clear();
    }
    abc::g_pFileSystem->Close(File);
    framework::ImageLoader::DecodedImage Image;
    if (!framework::decodePreview(Request, Bytes, Image))
      return 0;
    const int Texture =
        uploadSurfaceTexture(Image.Pixels.data(), Image.Width, Image.Height);
    if (!Texture)
      return 0;
    dimensions = {Image.Width, Image.Height};
    return retainTexture(Texture, true);
  }
  // Externally-owned live textures first (radar FBO): the owner registered
  // the URI prefix; the query suffix is a cache-bust, the current handle
  // lives in the registry.
  for (const auto &[prefix, external] : ExternalTextures) {
    if (source.rfind(prefix, 0) != 0 || !external.Texture)
      continue;
    dimensions.x = external.Width;
    dimensions.y = external.Height;
    return retainTexture(static_cast<int>(external.Texture), false, prefix);
  }
  // Sprite URIs ("spr://<hud.txt name>", "spricon://<atlas>/<l>,<t>,<r>,<b>",
  // "spralpha://...") resolve through the sprite data layer, never the file
  // interface (the scheme prefix also keeps Rml from joining the document
  // directory).
  if (source.rfind("spr://", 0) == 0 || source.rfind("spricon://", 0) == 0) {
    int width = 0, height = 0;
    const Rml::TextureHandle handle =
        retainTexture(sprites::textureForUri(source, width, height), false);
    if (handle) {
      dimensions.x = width;
      dimensions.y = height;
      // Old ImageSprPanel rendered HUD sprites kRenderTransAdd - mark
      // the handle so RenderGeometry picks the additive program.
      AdditiveTextures.insert(handle);
    }
    return handle;
  }
  if (source.rfind("avatar://", 0) == 0) {
    // avatar://<steamid64>[?gen] - scoreboard avatars (data::avatars);
    // the scoreboard bumps `gen` when an avatar lands so Rml re-resolves.
    const std::string body = source.substr(9);
    const uint64_t steamId = strtoull(body.c_str(), nullptr, 10);
    int width = 0, height = 0;
    const Rml::TextureHandle handle =
        retainTexture(avatars::textureFor(steamId, width, height, true), false);
    if (handle) {
      dimensions.x = width;
      dimensions.y = height;
    }
    return handle;
  }
  if (source.rfind("spralpha://", 0) == 0) {
    int width = 0, height = 0;
    const Rml::TextureHandle handle =
        retainTexture(sprites::textureForUri(source, width, height), false);
    if (handle) {
      dimensions.x = width;
      dimensions.y = height;
      // spralpha:// is the straight-alpha crop flavor.
    }
    return handle;
  }
  if (source.rfind("mod://", 0) == 0) {
    // Module package image: resolve through the module VFS, decode from the
    // (zero-copy) entry view.
    return loadModuleTexture(dimensions, source, false);
  }
  if (source.rfind("modadd://", 0) == 0) {
    // Additive package-image twin of mod://.
    return loadModuleTexture(dimensions, "mod://" + source.substr(9), true);
  }
  // No implicit engine-path lookup and no legacy loose-resource schemes.
  return 0;
}

Rml::TextureHandle
AbcRenderInterface::loadFileTexture(Rml::Vector2i &dimensions,
                                    const Rml::String &source) {
  // `source` is the RML-referenced path (Rml joins the document directory);
  // resolve against the mod dir like every other abcui asset. Decoding goes
  // through the shared FreeImage loader - format is detected from the file
  // content, so RML sources stay extension-agnostic.
  framework::ImageLoader::DecodedImage image;
  if (!framework::ImageLoader::loadFromFile(source.c_str(), image))
    return 0;

  const int textureId =
      uploadSurfaceTexture(image.Pixels.data(), image.Width, image.Height);
  if (textureId == 0)
    return 0;
  dimensions = Rml::Vector2i(image.Width, image.Height);
  return retainTexture(textureId, true);
}

Rml::TextureHandle
AbcRenderInterface::loadModuleTexture(Rml::Vector2i &dimensions,
                                      const Rml::String &uri, bool additive) {
  // Module package image: resolve through the module VFS, decode from the
  // (zero-copy) entry view.
  framework::PackageEntry entry;
  if (!framework::ModuleVfs::open(uri.c_str(), entry, nullptr))
    return 0;
  framework::ImageLoader::DecodedImage image;
  if (!framework::ImageLoader::loadFromMemory(
          {static_cast<const std::byte *>(entry.data), entry.size}, image))
    return 0;
  const int textureId =
      uploadSurfaceTexture(image.Pixels.data(), image.Width, image.Height);
  if (!textureId)
    return 0;
  dimensions = Rml::Vector2i(image.Width, image.Height);
  const Rml::TextureHandle handle = retainTexture(textureId, true);
  if (handle && additive)
    AdditiveTextures.insert(handle);
  return handle;
}

Rml::TextureHandle
AbcRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source,
                                    Rml::Vector2i dimensions) {
  size_t bytes;
  if (!framework::ResourceLimits::imageBytes(dimensions.x, dimensions.y,
                                             bytes) ||
      source.size() < bytes)
    return 0;

  // RmlUi font/gradient atlases arrive premultiplied; straighten to match
  // our SRC_ALPHA blend (see header note).
  std::vector<unsigned char> pixels(source.begin(), source.end());
  UnpremultiplyInPlace(pixels.data(),
                       static_cast<size_t>(dimensions.x) * dimensions.y);

  const int textureId =
      uploadSurfaceTexture(pixels.data(), dimensions.x, dimensions.y);
  if (textureId == 0)
    return 0;
  return retainTexture(textureId, true);
}

int AbcRenderInterface::uploadSurfaceTexture(const unsigned char *rgba,
                                             int width, int height) {
  // The shared surface channel ends any active 2D pass before uploading.
  // The next draw reinstalls the viewport and projection lazily.
  const int id = vgui2ext::createTexture();
  if (id == 0) {
    static bool Warned = false;
    if (!Warned) {
      abc::ConsoleWarn("[AbcUi] texture upload unavailable (VGUI2Extension "
                       "surface missing)\n");
      Warned = true;
    }
    return 0;
  }
  if constexpr (kFlipTextureUpload) {
    std::vector<unsigned char> flipped(rgba, rgba + static_cast<size_t>(width) *
                                                        height * 4);
    FlipRows(flipped.data(), width, height);
    vgui2ext::uploadTexture(id, flipped.data(), width, height);
  } else {
    vgui2ext::uploadTexture(id, rgba, width, height);
  }
  return id;
}

Rml::TextureHandle AbcRenderInterface::retainTexture(int id, bool owned,
                                                     Rml::String external) {
  if (!id)
    return 0;
  const auto handle = NextTexture++;
  Textures.emplace(handle, TextureRecord{id, owned, std::move(external)});
  return handle;
}

void AbcRenderInterface::ReleaseTexture(Rml::TextureHandle texture) {
  if (texture == 0)
    return;
  AdditiveTextures.erase(texture);
  const auto record = Textures.find(texture);
  if (record == Textures.end())
    return;
  if (record->second.Owned)
    vgui2ext::deleteTextureById(record->second.Id);
  Textures.erase(record);
}

void AbcRenderInterface::clearAdditiveTextures() {
  AdditiveTextures.clear();
}

void AbcRenderInterface::EnableScissorRegion(bool enable) {
  ScissorEnabled = enable;
  if (!enable)
    ScissorRect = Rml::Rectanglei::MakeInvalid();
}

void AbcRenderInterface::SetScissorRegion(Rml::Rectanglei region) {
  ScissorRect = region;
}

void AbcRenderInterface::SetTransform(const Rml::Matrix4f *transform) {
  TransformActive = transform != nullptr;
  if (transform)
    Transform = *transform;
}

Rml::Vector2f
AbcRenderInterface::applyTransform(const Rml::Vector2f &point) const {
  if (!TransformActive)
    return point;
  const Rml::Vector4f transformed =
      Transform * Rml::Vector4f(point.x, point.y, 0.0f, 1.0f);
  return Rml::Vector2f(transformed.x, transformed.y);
}

} // namespace rmlui
