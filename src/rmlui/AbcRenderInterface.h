#pragma once

// rmlui::AbcRenderInterface - Rml::RenderInterface implemented over
// IMetaRenderer (Renderer.dll). Plan 05 §3.2.
//
// Seam rules (09 §9.2): every GPU command goes through IMetaRenderer or the
// VGUI2 surface texture channel (vgui2ext helpers); no OpenGL calls and no
// engine headers leak past this header (only Rml + abc types).
//
// Key semantics:
// - Blending: the renderer's textured/filled rect paths only offer straight
//   alpha (SRC_ALPHA, ONE_MINUS_SRC_ALPHA). RmlUi hands out premultiplied
//   vertex colours and premultiplied font atlas bytes, so both are converted
//   to straight alpha here (un-premultiply; alpha 0 clamps to black).
// - Sprite textures (spr://, spricon://) draw with the alpha-based additive
//   program (SRC_ALPHA, ONE = kRenderTransAdd "src*a + dest") - the old
//   ImageSprPanel drew every HUD sprite icon that way, which both removes
//   the sprite's black background and lets the Rml opacity transition ride
//   the same alpha term (old panel-alpha fade).
// - Rml texture handles are opaque per-load records, separate from engine IDs.
//   Sprite/avatar caches own their IDs until shutdown; file/font records own
//   theirs.
// - Scissor (Spike R-1): IMetaRenderer has no glScissor equivalent, but
//   Set2DEx(x, y, w, h) installs viewport = rect AND an ortho projection
//   mapping local (0..w, 0..h) into it; rasterization outside a viewport is
//   clipped, so the scissor region is realized as Set2DEx + a vertex offset
//   by the region origin. Verified against Renderer pinned sources
//   (GL_Set2DEx in gl_rmain.cpp); see D-12 for the R-1 verdict.
// - 2D state ownership: RenderGeometry installs Set2D/Set2DEx lazily per
//   scissor state; beginFrame()/endFrame() bracket a render pass (endFrame
//   pops the matrix stack). Called by RmlUiModule around Context::Render().
// - Geometry baking (Phase 8, 05 §8): the premultiplied→straight colour
//   conversion and the int→uint32 index widening run ONCE at CompileGeometry;
//   RenderGeometry draws from the baked arrays with zero per-vertex CPU work.
//   The element translation (whole pixels - RmlUi rounds it) and the scissor
//   origin ride the renderer's world matrix (glTranslate semantics, the draw
//   reads world×proj per call), so the common path is: state machine + one
//   Draw call. That is the closest "Bind+Draw" the IMetaRenderer API allows:
//   its Draw* entries only accept CPU vertex arrays (its own ring-buffer
//   upload inside), and the no-direct-OpenGL red line forbids a caller-VAO
//   draw path of our own.
// - Transforms: Rml element transforms (rotate etc., batch B Indicator) pass
//   through SetTransform as a matrix in the same screen space as the compiled
//   vertices; while one is active RenderGeometry falls back to the CPU loop
//   (M·(v + translation), the GL2 sample order: translate first, then the
//   loaded matrix) into reused scratch buffers. Reset by RmlUi's
//   RenderManager identity reset whenever the next element is untransformed,
//   so nothing lingers across the frame.
// - Extra texture URIs handled here:
//     tex://<mod-relative path>    -> file loaded straight-alpha. The scheme
//     keeps Rml from joining the document directory, so markup composed in
//     code resolves from any document (framework::glyphs outline layer).
//     texadd://<mod-relative path> -> file loaded additive (old
//     kRenderTransAdd TGA panels: the pain indicator; the additive glyph ink).

#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Types.h>

#include "TextureBlend.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rmlui {

class AbcRenderInterface : public Rml::RenderInterface {
public:
  AbcRenderInterface() = default;
  ~AbcRenderInterface() override;

  // Bracket one render pass (see header note on 2D state ownership).
  void beginFrame();
  void endFrame();

  // Rml::RenderInterface - required set.
  Rml::CompiledGeometryHandle
  CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                  Rml::Span<const int> indices) override;
  void RenderGeometry(Rml::CompiledGeometryHandle geometry,
                      Rml::Vector2f translation,
                      Rml::TextureHandle texture) override;
  void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

  Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions,
                                 const Rml::String &source) override;
  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                     Rml::Vector2i dimensions) override;
  void ReleaseTexture(Rml::TextureHandle texture) override;

  void EnableScissorRegion(bool enable) override;
  void SetScissorRegion(Rml::Rectanglei region) override;
  void SetTransform(const Rml::Matrix4f *transform) override;

  // Forgets the additive-texture marks (SpriteResource bulk-deletes its
  // textures outside RmlUi on shutdown - stale ids must not tint regular
  // textures that later reuse them).
  void clearAdditiveTextures();

  // Externally-owned live texture (batch D - the radar FBO). Requests whose
  // source starts with `uriPrefix` resolve to `texture` (never deleted by
  // ReleaseTexture; the owner frees it), carry the given pixel dimensions,
  // and draw through *maskSlot as a mask texture whenever it is non-zero
  // (radar shape select) - the old CRadarMapImage::Paint semantics. The
  // query suffix on the URI is a cache-bust only (the FBO texture id
  // changes when the engine regenerates frame buffers; the owner bumps the
  // suffix and re-registers). `blend` selects the draw program: the animated
  // image element registers its frames additive (old kRenderTransAdd
  // sprites), everything else keeps the straight-alpha default.
  void registerExternalTexture(
      const Rml::String &uriPrefix, Rml::TextureHandle texture, int width,
      int height, const int *maskSlot,
      TextureBlend blend = TextureBlend::StraightAlpha);
  void unregisterExternalTexture(const Rml::String &uriPrefix);

private:
  // Renderer-native baked vertex shapes, mirroring texturedrectvertex_t /
  // filledrectvertex_t from render/RenderCommon.h byte for byte (float[2]/
  // float[4] members = vec2_t/vec4_t). Kept local so the engine headers
  // stay on the .cpp side of the seam; the static_asserts there pin the
  // layout, so a Renderer-side drift cannot pass silently (R-14 lesson).
  struct BakedTexturedVertex {
    float pos[2];
    float texcoord[2];
    float col[4];
  };
  struct BakedFilledVertex {
    float pos[2];
    float col[4];
  };

  struct CompiledGeometry {
    // Positions stay in Rml space (translation is per-render-call and
    // rides the world matrix); colours are converted at compile time.
    std::vector<BakedTexturedVertex> Textured;
    std::vector<uint32_t> Indices;
    // Untextured draws need the 16-byte filled shape; baked lazily on the
    // first untextured draw and reused (solid-colour meshes draw every
    // frame, and the texture of a mesh never changes after RmlUi built it).
    std::vector<BakedFilledVertex> Filled;
  };

  // Applies the active Rml transform (if any) to a screen-space point.
  Rml::Vector2f applyTransform(const Rml::Vector2f &point) const;
  // File decode behind both the plain-path and texadd:// calls goes
  // through framework::ImageLoader (FreeImage); this wrapper uploads the
  // result to the VGUI2 surface texture channel.
  Rml::TextureHandle loadFileTexture(Rml::Vector2i &dimensions,
                                     const Rml::String &source);
  // Uploads pixels through the VGUI2 surface texture channel, ending any
  // active 2D pass first (surface uploads are not re-entrant with the
  // renderer's batched pass - see the .cpp note). 0 = upload unavailable.
  int uploadSurfaceTexture(const unsigned char *rgba, int width, int height);

  // External live textures (see registerExternalTexture) plus the handles
  // this interface created itself through the VGUI surface channel: only
  // owned handles may be deleted on ReleaseTexture (external ones - the
  // radar FBO - belong to their owner).
  struct ExternalTexture {
    Rml::TextureHandle Texture = 0;
    int Width = 0;
    int Height = 0;
    const int *MaskSlot = nullptr;
    TextureBlend Blend = TextureBlend::StraightAlpha;
  };
  std::unordered_map<Rml::String, ExternalTexture> ExternalTextures;
  struct TextureRecord {
    int Id;
    bool Owned;
    Rml::String ExternalPrefix;
  };
  std::unordered_map<Rml::TextureHandle, TextureRecord> Textures;
  Rml::TextureHandle NextTexture = 1;
  Rml::TextureHandle retainTexture(int id, bool owned,
                                   Rml::String external = {});

  bool ScissorEnabled = false;
  Rml::Rectanglei ScissorRect = Rml::Rectanglei::MakeInvalid();
  // Mirrors what the renderer is currently set up for, so consecutive
  // geometries under one scissor state skip redundant Set2D calls.
  bool TwoDActive = false;
  bool AppliedScissor = false;
  Rml::Rectanglei AppliedScissorRect = Rml::Rectanglei::MakeInvalid();
  // Sprite textures draw additive (see header note).
  std::unordered_set<Rml::TextureHandle> AdditiveTextures;
  // Current element transform (null = identity); RmlUi only calls
  // SetTransform when the value actually changes (RenderManager).
  Rml::Matrix4f Transform = Rml::Matrix4f::Identity();
  bool TransformActive = false;
  // Transform-path scratch (transformed elements only): sized per call, no
  // reallocation after the first few frames.
  std::vector<BakedTexturedVertex> ScratchTextured;
  std::vector<BakedFilledVertex> ScratchFilled;
};

} // namespace rmlui
