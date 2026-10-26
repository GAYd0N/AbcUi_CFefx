#pragma once

// src/render/IRenderBackend.h - the framework-facing drawing contract
// (issue #24). Everything under src/ draws through this interface; no
// translation unit outside backends/ may include Renderer, GL or engine
// renderer headers. The ABI mirror is sdk/abcui/render_backend.h and the
// composition root (who loads a concrete backend) is BackendHost.
//
// The interface owns exactly: 2D drawing, surface size, the frame beat and
// the optional capability sub-passes. Texture upload is deliberately NOT
// here - the VGUI2 surface channel (vgui2ext) already yields engine texture
// ids that every backend can bind, so it stays framework-side.

#include <cstddef>
#include <cstdint>

namespace abc::render {

enum class Blend {
  StraightAlpha,     // SRC_ALPHA, ONE_MINUS_SRC_ALPHA
  Additive,          // SRC_ALPHA, ONE (kRenderTransAdd)
  PremultipliedAlpha // ONE, ONE_MINUS_SRC_ALPHA
};

enum class Capability {
  RenderCallbacks,  // backend drives the frame (Renderer: end-frame beat)
  RedrawBeat,       // framework pumps it from HUD_Redraw (GL)
  WorldPass,        // radar top-down world render
  BlurPass,         // menu backdrop blur
  OffscreenTargets, // viewport-capture textures (model preview)
};

struct Vertex {
  float position[2];
  float texcoord[2];
  float color[4];
};
struct FilledVertex {
  float position[2];
  float color[4];
};

struct Draw {
  const Vertex *vertices = nullptr;
  size_t vertexCount = 0;
  const uint32_t *indices = nullptr; // triangle list
  size_t indexCount = 0;
  int texture = 0;     // engine texture id
  int maskTexture = 0; // 0 = none
  Blend mode = Blend::StraightAlpha;
  const char *label = ""; // debug metadata
};

struct FilledDraw {
  const FilledVertex *vertices = nullptr;
  size_t vertexCount = 0;
  const uint32_t *indices = nullptr;
  size_t indexCount = 0;
  Blend mode = Blend::StraightAlpha;
  const char *label = "";
};

// Implemented by the framework (rmlui); the backend calls it at the right
// moment. A null optional beat simply never fires.
class IFrameSink {
public:
  virtual ~IFrameSink() = default;
  // Draw the UI pass into the backend's final target.
  virtual void renderFrame() = 0;
  // Backend FBOs died (vid restart / regeneration); drop texture ids and
  // external registrations.
  virtual void invalidateTargets() = 0;
  // The backend's world-render beat (Renderer: pre-render-view). The radar
  // world pass runs here, inside the backend's frame.
  virtual void worldPassBeat() = 0;
};

// Radar world pass (Capability::WorldPass). The implementation owns its
// render target; the framework owns cvar save/restore and camera math.
class IWorldPass {
public:
  virtual ~IWorldPass() = default;
  struct Request {
    int width = 0;
    int height = 0;
    float origin[3] = {0, 0, 0};
    float angles[3] = {0, 0, 0};
    float orthoHalfExtent = 0;
    float orthoNear = 0;
    float orthoFar = 0;
    float clearColor[4] = {0, 0, 0, 1};
  };
  // Creates/resizes the target; returns its colour texture id (0 = fail).
  virtual unsigned int ensureTarget(int width, int height) = 0;
  virtual void freeTarget() = 0;
  virtual unsigned int texture() const = 0; // 0 = no live target
  virtual void render(const Request &request) = 0;
};

// Menu backdrop blur (Capability::BlurPass).
class IBlurPass {
public:
  virtual ~IBlurPass() = default;
  struct Request {
    int width = 0;
    int height = 0;
    unsigned int sourceTexture = 0; // 0 = the backend's final buffer
    int rounds = 0;                 // gaussian rounds; 0 keeps the capture
  };
  // White-fill + capture + blur; returns the resulting texture id, or 0
  // when no source is available this frame (callers degrade to solid).
  virtual unsigned int render(const Request &request) = 0;
  virtual void invalidate() = 0;
};

// Viewport-capture textures (Capability::OffscreenTargets) for the
// framework-side model preview pass: the pass renders into the backend's
// currently bound framebuffer, clears its viewport region and copies the
// completed view region out. A backend may retain the pre-composite scene
// as the capture source to preserve alpha; the copied image is straight-
// alpha RGBA8, ready for the ordinary alpha blend. Handles are
// backend-defined; 0 is never valid.
class IOffscreenTargets {
public:
  virtual ~IOffscreenTargets() = default;
  virtual unsigned int create(int width, int height) = 0;
  virtual void free(unsigned int target) = 0;
  virtual unsigned int texture(unsigned int target) const = 0;
  virtual void clearViewportRegion(int x, int y, int width, int height,
                                   const float rgba[4]) = 0;
  virtual void copyViewportToTarget(unsigned int target, int x, int y,
                                    int width, int height) = 0;
};

class IRenderBackend {
public:
  virtual ~IRenderBackend() = default;
  virtual const char *id() const = 0;
  virtual bool supports(Capability capability) const = 0;
  virtual int surfaceWidth() const = 0;
  virtual int surfaceHeight() const = 0;

  virtual bool installFrameSink(IFrameSink *sink) = 0;
  virtual void removeFrameSink() = 0;
  virtual void pumpRedraw(double seconds) = 0; // RedrawBeat: fires the sink

  virtual void beginFrame() = 0; // bind the final target, save state
  virtual void endFrame() = 0;
  virtual void set2D(int x, int y, int width, int height) = 0;
  virtual void finish2D() = 0;
  virtual void pushTranslation(float x, float y) = 0;
  virtual void popTranslation() = 0;
  virtual void drawTextured(const Draw &draw) = 0;
  virtual void drawFilled(const FilledDraw &draw) = 0;

  // Capability sub-interfaces; nullptr means "not supported" and must route
  // the caller into its existing fallback branch, never a crash.
  virtual IWorldPass *worldPass() = 0;
  virtual IBlurPass *blurPass() = 0;
  virtual IOffscreenTargets *targets() = 0;
};

} // namespace abc::render
