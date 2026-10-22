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
#include "render/Render.h"
#include "render/RenderCommon.h"
#include "VGUI2ExtensionImport.h"

namespace rmlui {

namespace {

// Upload row order of the VGUI2 surface DrawSetTextureRGBA channel, settled
// by the Phase 3 in-game spike: false = uploads land top-scanline-first at
// v=0, which matches RmlUi's (0,0)=top-left UV convention without extra
// flipping (the image itself is flipped out of FreeImage's bottom-up order
// in LoadTexture below).
constexpr bool kFlipTextureUpload = false;

constexpr uint64_t kProgramAlphaBlend = DRAW_TEXTURED_RECT_ALPHA_BLEND_ENABLED;
constexpr uint64_t kProgramFilledAlphaBlend =
    DRAW_FILLED_RECT_ALPHA_BLEND_ENABLED;
// Sprite icons: kRenderTransAdd parity (src*a + dest) - see header note.
constexpr uint64_t kProgramSpriteAdditive =
    DRAW_TEXTURED_RECT_ALPHA_BASED_ADDITIVE_ENABLED;

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
    if (IMetaRenderer *renderer = abc::MetaRenderer())
      renderer->Finish2D();
    TwoDActive = false;
    AppliedScissor = false;
    AppliedScissorRect = Rml::Rectanglei::MakeInvalid();
  }
}

Rml::CompiledGeometryHandle
AbcRenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                    Rml::Span<const int> indices) {
  // The baked shapes must stay byte-identical to the renderer's vertex
  // types - the Draw* calls copy our arrays through memcpy (R-14 lesson:
  // mirror layouts are pinned here, drift cannot pass silently).
  static_assert(sizeof(BakedTexturedVertex) == sizeof(texturedrectvertex_t));
  static_assert(offsetof(BakedTexturedVertex, texcoord) ==
                offsetof(texturedrectvertex_t, texcoord));
  static_assert(offsetof(BakedTexturedVertex, col) ==
                offsetof(texturedrectvertex_t, col));
  static_assert(sizeof(BakedFilledVertex) == sizeof(filledrectvertex_t));
  static_assert(offsetof(BakedFilledVertex, col) ==
                offsetof(filledrectvertex_t, col));

  if (vertices.empty() || indices.empty())
    return {};

  auto geometry = new CompiledGeometry();
  // One-time bake (05 §8): colours un-premultiplied, indices widened -
  // RenderGeometry never repeats this work.
  geometry->Textured.resize(vertices.size());
  for (size_t i = 0; i < vertices.size(); i++) {
    const Rml::Vertex &v = vertices[i];
    float *pos = geometry->Textured[i].pos;
    float *texcoord = geometry->Textured[i].texcoord;
    pos[0] = v.position.x;
    pos[1] = v.position.y;
    texcoord[0] = v.tex_coord.x;
    texcoord[1] = v.tex_coord.y;
    ToStraightColor(v.colour, geometry->Textured[i].col);
  }
  geometry->Indices.reserve(indices.size());
  for (const int index : indices)
    geometry->Indices.push_back(static_cast<uint32_t>(index));
  return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
}

void AbcRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry,
                                        Rml::Vector2f translation,
                                        Rml::TextureHandle texture) {
  IMetaRenderer *renderer = abc::MetaRenderer();
  if (!renderer || geometry == 0)
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

  // Per-state 2D setup (matrix push/pop balanced by endFrame).
  if (!TwoDActive || AppliedScissor != scissor ||
      (scissor && !(AppliedScissorRect == ScissorRect))) {
    endFrame();
    if (scissor) {
      // Set2DEx takes GL window coords (bottom-left origin); Rml rects
      // are top-left origin, hence the y flip around screen height.
      const int screenH = static_cast<int>(renderer->GetSwapChainHeight());
      renderer->Set2DEx(ScissorRect.Left(), screenH - ScissorRect.Bottom(),
                        ScissorRect.Width(), ScissorRect.Height());
    } else {
      renderer->Set2D();
    }
    TwoDActive = true;
    AppliedScissor = scissor;
    AppliedScissorRect = ScissorRect;
  }

  // The common path's whole per-frame work: hand the translation (element
  // offset minus the Set2DEx re-basing) to the world matrix - glTranslate
  // semantics, and the draw reads world×proj per call - and draw straight
  // from the baked arrays. Both parts are whole pixels (RmlUi rounds its
  // render translations; scissor rects are integer), so this is exactly the
  // old `applyTransform(v.position + translation)` under an identity Rml
  // transform, computed on the GPU.
  const float offsetX =
      translation.x - (scissor ? static_cast<float>(ScissorRect.Left()) : 0.0f);
  const float offsetY =
      translation.y - (scissor ? static_cast<float>(ScissorRect.Top()) : 0.0f);
  const bool translateOnGpu =
      !TransformActive && (offsetX != 0.0f || offsetY != 0.0f);
  if (translateOnGpu) {
    renderer->PushWorldMatrix();
    renderer->TranslateWorldMatrix(offsetX, offsetY, 0.0f);
  }

  const BakedTexturedVertex *texturedVertices = source->Textured.data();
  const BakedFilledVertex *filledVertices = nullptr;
  const BakedTexturedVertex *scratchTextured = nullptr;
  if (TransformActive) {
    // Rare path (CSS-transformed elements): M·(v + translation) - the
    // GL2 sample order: translate first, then the loaded matrix - minus
    // the scissor re-basing, per vertex, into reused scratch.
    const bool wantFilled = texture == 0;
    if (wantFilled) {
      ScratchFilled.resize(vertexCount);
      for (size_t i = 0; i < vertexCount; i++) {
        const Rml::Vector2f pos =
            applyTransform(Rml::Vector2f(source->Textured[i].pos[0],
                                         source->Textured[i].pos[1]) +
                           translation);
        ScratchFilled[i].pos[0] =
            pos.x - (scissor ? static_cast<float>(ScissorRect.Left()) : 0.0f);
        ScratchFilled[i].pos[1] =
            pos.y - (scissor ? static_cast<float>(ScissorRect.Top()) : 0.0f);
        ScratchFilled[i].col[0] = source->Textured[i].col[0];
        ScratchFilled[i].col[1] = source->Textured[i].col[1];
        ScratchFilled[i].col[2] = source->Textured[i].col[2];
        ScratchFilled[i].col[3] = source->Textured[i].col[3];
      }
      filledVertices = ScratchFilled.data();
    } else {
      ScratchTextured.resize(vertexCount);
      for (size_t i = 0; i < vertexCount; i++) {
        const BakedTexturedVertex &v = source->Textured[i];
        const Rml::Vector2f pos =
            applyTransform(Rml::Vector2f(v.pos[0], v.pos[1]) + translation);
        BakedTexturedVertex &out = ScratchTextured[i];
        out.pos[0] =
            pos.x - (scissor ? static_cast<float>(ScissorRect.Left()) : 0.0f);
        out.pos[1] =
            pos.y - (scissor ? static_cast<float>(ScissorRect.Top()) : 0.0f);
        out.texcoord[0] = v.texcoord[0];
        out.texcoord[1] = v.texcoord[1];
        out.col[0] = v.col[0];
        out.col[1] = v.col[1];
        out.col[2] = v.col[2];
        out.col[3] = v.col[3];
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
        baked->Filled[i].pos[0] = source->Textured[i].pos[0];
        baked->Filled[i].pos[1] = source->Textured[i].pos[1];
        baked->Filled[i].col[0] = source->Textured[i].col[0];
        baked->Filled[i].col[1] = source->Textured[i].col[1];
        baked->Filled[i].col[2] = source->Textured[i].col[2];
        baked->Filled[i].col[3] = source->Textured[i].col[3];
      }
    }
    filledVertices = baked->Filled.data();
  }

  // Texture handles are surface texture ids (int namespace). The baked
  // shapes are layout-identical to the renderer's vertex types (static
  // asserts at CompileGeometry), so the casts below are byte-transparent.
  if (texture == 0) {
    renderer->DrawFilledRect(
        reinterpret_cast<const filledrectvertex_t *>(filledVertices),
        vertexCount, source->Indices.data(), indexCount,
        kProgramFilledAlphaBlend, "AbcUi filled");
  } else {
    const auto record = Textures.find(texture);
    if (record == Textures.end()) {
      if (translateOnGpu)
        renderer->PopWorldMatrix();
      return;
    }
    int textureId = record->second.Id;
    if (!record->second.ExternalPrefix.empty()) {
      const auto live = ExternalTextures.find(record->second.ExternalPrefix);
      if (live == ExternalTextures.end()) {
        if (translateOnGpu)
          renderer->PopWorldMatrix();
        return;
      }
      textureId = static_cast<int>(live->second.Texture);
    }
    const auto *drawVertices = reinterpret_cast<const texturedrectvertex_t *>(
        TransformActive ? scratchTextured : texturedVertices);
    uint64_t program = AdditiveTextures.count(texture) ? kProgramSpriteAdditive
                                                       : kProgramAlphaBlend;
    if (!record->second.ExternalPrefix.empty()) {
      const auto live = ExternalTextures.find(record->second.ExternalPrefix);
      if (live != ExternalTextures.end()) {
        switch (live->second.Blend) {
        case TextureBlend::Additive:
          program = kProgramSpriteAdditive;
          break;
        case TextureBlend::PremultipliedAlpha:
          program = DRAW_TEXTURED_RECT_PREMULTIPLIED_ALPHA_ENABLED;
          break;
        case TextureBlend::StraightAlpha:
          break;
        }
      }
    }
    // External masked textures (radar map): the mask id rides the registered
    // slot, read live so a shape toggle needs no texture reload (the Rml
    // cache holds one entry for the URI).
    bool drewMasked = false;
    for (const auto &[prefix, external] : ExternalTextures) {
      if (record->second.ExternalPrefix != prefix || !external.MaskSlot ||
          !*external.MaskSlot)
        continue;
      program |= DRAW_TEXTURED_RECT_MASK_TEXTURE_ENABLED;
      renderer->DrawTexturedRectMask(
          textureId, *external.MaskSlot, drawVertices, vertexCount,
          source->Indices.data(), indexCount, program, "AbcUi masked");
      drewMasked = true;
      break;
    }
    if (!drewMasked)
      renderer->DrawTexturedRect(textureId, drawVertices, vertexCount,
                                 source->Indices.data(), indexCount, program,
                                 "AbcUi");
  }

  if (translateOnGpu)
    renderer->PopWorldMatrix();
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
  for (auto &[handle, record] : Textures) {
    (void)handle;
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
  delete reinterpret_cast<CompiledGeometry *>(geometry);
}

Rml::TextureHandle AbcRenderInterface::LoadTexture(Rml::Vector2i &dimensions,
                                                   const Rml::String &source) {
  // engine:// lets package documents reference shared framework art by its
  // absolute engine path; the rest of the chain below speaks plain paths.
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
    std::vector<unsigned char> Bytes;
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
  if (source.rfind("tex://", 0) == 0) {
    // Mod-relative file, straight alpha: the non-additive twin of texadd://.
    // The scheme keeps Rml from joining the document directory, so markup
    // composed in code (the additive glyph stacks) resolves from anywhere.
    return loadFileTexture(dimensions, source.substr(6));
  }
  if (source.rfind("mod://", 0) == 0) {
    // Module package image: resolve through the module VFS, decode from the
    // (zero-copy) entry view.
    return loadModuleTexture(dimensions, source, false);
  }
  if (source.rfind("modadd://", 0) == 0) {
    // Additive twin of mod:// - the package-internal flavor of texadd://.
    return loadModuleTexture(dimensions, "mod://" + source.substr(9), true);
  }
  if (source.rfind("texadd://", 0) == 0) {
    // Mod-relative file loaded additive (old kRenderTransAdd TGA panels -
    // the pain indicator arc). Shares the FreeImage path below.
    const Rml::TextureHandle handle =
        loadFileTexture(dimensions, source.substr(9));
    if (handle)
      AdditiveTextures.insert(handle);
    return handle;
  }

  // `source` is the RML-referenced path (Rml joins the document directory);
  // resolve against the mod dir like every other abcui asset.
  return loadFileTexture(dimensions, source);
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
          static_cast<const unsigned char *>(entry.data), entry.size, image))
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
  // The upload goes through the VGUI2 surface's own texture path, which is
  // not re-entrant with the renderer's batched 2D pass: an upload issued
  // between queued 2D draws leaves the freshly uploaded texture sampling as
  // opaque black (live-caught 2026-10-16: the create-game page's map list
  // introduced new glyphs, RmlUi regenerated the font atlases in the middle
  // of the frame and every glyph of the page painted as a black rectangle).
  // Closing the pass first hands the renderer a clean batch; the next draw
  // re-installs Set2D/Set2DEx (the usual pair discipline).
  endFrame();
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
  if (record->second.Owned) {
    // A font-atlas rebuild releases the previous generation while the
    // current Rml pass may already have queued text draws against it.
    // Deleting the VGUI texture before flushing that batch leaves those
    // draws sampling a destroyed id (all affected glyphs become black
    // rectangles until the next pass rebuilds its geometry).
    endFrame();
    vgui2ext::deleteTextureById(record->second.Id);
  }
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
