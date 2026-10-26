#pragma once

// rmlui::AbcRenderInterface - Rml::RenderInterface implemented over the
// injected render backend (abc::render::IRenderBackend, issue #24).
//
// Seam rules (09 §9.2): every GPU command goes through the backend or the
// VGUI2 surface texture channel (vgui2ext helpers); no Renderer headers, no
// OpenGL calls and no engine headers leak past this header (only Rml + abc
// types).
//
// Key semantics:
// - Blending: the draw contract carries a semantic blend mode. RmlUi hands
//   out premultiplied vertex colours and premultiplied font atlas bytes,
//   and the backend's straight-alpha path expects straight colour, so both
//   are converted here (un-premultiply; alpha 0 clamps to black).
// - Sprite textures (spr://, spricon://) draw additive (SRC_ALPHA, ONE =
//   kRenderTransAdd "src*a + dest") - the old ImageSprPanel drew every HUD
//   sprite icon that way, which both removes the sprite's black background
//   and lets the Rml opacity transition ride the same alpha term (old
//   panel-alpha fade).
// - Rml texture handles are opaque per-load records, separate from engine IDs.
//   Sprite/avatar caches own their IDs until shutdown; file/font records own
//   theirs.
// - Scissor (Spike R-1): the backend's set2D(x, y, w, h) installs
//   viewport = rect (rasterization outside a viewport is clipped), so the
//   scissor region is realized as set2D + a vertex offset by the region
//   origin. Verified against Renderer pinned sources (GL_Set2DEx in
//   gl_rmain.cpp); see D-12 for the R-1 verdict.
// - 2D state ownership: RenderGeometry installs set2D lazily per scissor
//   state; beginFrame()/endFrame() bracket a render pass (endFrame pops the
//   2D state). Called by the frame sink around Context::Render().
// - Geometry baking (Phase 8, 05 §8): the premultiplied→straight colour
//   conversion and the int→uint32 index widening run ONCE at
//   CompileGeometry; RenderGeometry draws from the baked arrays with zero
//   per-vertex CPU work. The element translation (whole pixels - RmlUi
//   rounds it) and the scissor origin ride the backend's translation stack
//   (push/pop semantics, the draw applies them per call), so the common
//   path is: state machine + one draw call.
// - Transforms: Rml element transforms (rotate etc., batch B Indicator) pass
//   through SetTransform as a matrix in the same screen space as the compiled
//   vertices; while one is active RenderGeometry falls back to the CPU loop
//   (M·(v + translation), the GL2 sample order: translate first, then the
//   loaded matrix) into reused scratch buffers. Reset by RmlUi's
//   RenderManager identity reset whenever the next element is untransformed,
//   so nothing lingers across the frame.
// - Module images use mod:// or additive modadd://; uploaded module textures
//   use moduletex://. Engine content uses explicit engine:// / sprite URIs.

#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Types.h>

#include "../render/IRenderBackend.h"
#include "TextureBlend.h"

#include <cstdint>
#include <memory>
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

  // Externally-owned live texture (radar FBO, module uploads and media).
  // Requests whose source starts with `uriPrefix` resolve to `texture` (never
  // deleted by ReleaseTexture; the owner frees it), carry the given pixel
  // dimensions, and draw through *maskSlot as a mask texture whenever it is
  // non-zero (radar shape select) - the old CRadarMapImage::Paint semantics.
  // The query suffix on the URI is a cache-bust only (the FBO texture id
  // changes when the engine regenerates frame buffers; the owner bumps the
  // suffix and re-registers). `blend` selects the draw mode: the animated
  // image element registers its frames additive (old kRenderTransAdd
  // sprites), everything else keeps the straight-alpha default.
  void
  registerExternalTexture(const Rml::String &uriPrefix,
                          Rml::TextureHandle texture, int width, int height,
                          const int *maskSlot,
                          TextureBlend blend = TextureBlend::StraightAlpha);
  void unregisterExternalTexture(const Rml::String &uriPrefix);

private:
  // Baked shapes use the backend contract's vertex PODs (abc::render::Vertex
  // / FilledVertex); the backend pins them against its native layout, so
  // drift cannot pass silently (R-14 lesson).
  struct CompiledGeometry {
    // Positions stay in Rml space (translation is per-render-call and rides
    // the backend's translation stack); colours are converted at compile
    // time.
    std::vector<abc::render::Vertex> Textured;
    std::vector<uint32_t> Indices;
    // Untextured draws need the filled shape; baked lazily on the first
    // untextured draw and reused (solid-colour meshes draw every frame, and
    // the texture of a mesh never changes after RmlUi built it).
    std::vector<abc::render::FilledVertex> Filled;
  };

  // Applies the active Rml transform (if any) to a screen-space point.
  Rml::Vector2f applyTransform(const Rml::Vector2f &point) const;
  // Explicit engine:// image decoding goes through FreeImage; upload uses
  // result to the VGUI2 surface texture channel.
  Rml::TextureHandle loadFileTexture(Rml::Vector2i &dimensions,
                                     const Rml::String &source);
  // mod:// and modadd:// twin: decodes a module package image through the
  // module VFS entry view.
  Rml::TextureHandle loadModuleTexture(Rml::Vector2i &dimensions,
                                       const Rml::String &uri, bool additive);
  // Uploads pixels through the shared VGUI2 surface texture channel, which
  // ends any active 2D pass before mutation. 0 = upload unavailable.
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
  // Baked geometry is owned through the handle RmlUi returns; releasing the
  // handle erases (and destroys) the record.
  std::unordered_map<Rml::CompiledGeometryHandle,
                     std::unique_ptr<CompiledGeometry>>
      Geometries;
  Rml::TextureHandle NextTexture = 1;
  Rml::TextureHandle retainTexture(int id, bool owned,
                                   Rml::String external = {});

  bool ScissorEnabled = false;
  Rml::Rectanglei ScissorRect = Rml::Rectanglei::MakeInvalid();
  // Mirrors what the backend is currently set up for, so consecutive
  // geometries under one scissor state skip redundant set2D calls.
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
  std::vector<abc::render::Vertex> ScratchTextured;
  std::vector<abc::render::FilledVertex> ScratchFilled;
};

} // namespace rmlui
