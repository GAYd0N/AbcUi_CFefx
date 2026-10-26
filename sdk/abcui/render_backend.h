#pragma once

#include <stdint.h>

// sdk/abcui/render_backend.h - the render-backend process boundary (issue
// #24). Same pattern as module_api.h: versioned C struct tables, POD
// payloads, append-only members, x86 cdecl.
//
// A render backend DLL exports exactly one function:
//
//   ABCUI_RENDER_BACKEND_EXPORT int32_t ABCUI_RENDER_BACKEND_CALL
//   AbcRenderBackend_Main(uint32_t abiVersion,
//                         const AbcUiRenderBackendHost *host,
//                         AbcUiRenderBackendDesc *outDesc);
//
// Backends live as loose DLLs in <mod>/abc/abcui/render_backend/ and are
// selected with the abc_ui_render_backend cvar (full restart to apply). A
// missing or unusable selected backend is fatal at startup - there is no
// built-in fallback. A backend DLL:
//   - must not include framework headers or link the framework's static
//     libraries; everything arrives through the host table and the
//     descriptor it fills;
//   - may include third-party SDK headers (MetaHook, Renderer, GL) freely -
//     it is the only place in the AbcUi tree where Renderer headers are
//     allowed next to the gl backend;
//   - owns exactly: 2D drawing, the surface size, the frame beat and the
//     capability sub-passes declared below. Texture UPLOAD stays
//     framework-side (the VGUI2 surface channel yields engine texture ids
//     every backend can bind);
//   - must not throw across this ABI and must not unload itself.
//
// All callbacks run on the engine main thread. Append-only: new members go
// at the end of a struct and readers must honor the caller's structSize.

#include "abi.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bumped on any breaking change to the structs below.
#define ABCUI_RENDER_BACKEND_ABI_VERSION 1

#ifdef __cplusplus
#define ABCUI_RENDER_BACKEND_EXPORT extern "C" __declspec(dllexport)
#else
#define ABCUI_RENDER_BACKEND_EXPORT __declspec(dllexport)
#endif

// Same cdecl rule as ABCUI_CALL (abi.h), named for this boundary.
#define ABCUI_RENDER_BACKEND_CALL ABCUI_CALL

// Capability bits (AbcUiRenderBackendDesc::capabilities).
#define ABCUI_RB_CAP_RENDER_CALLBACKS 0x1u   // backend drives the frame beat
#define ABCUI_RB_CAP_REDRAW_BEAT 0x2u        // framework pumps HUD_Redraw
#define ABCUI_RB_CAP_WORLD_PASS 0x4u         // top-down world render (radar)
#define ABCUI_RB_CAP_BLUR_PASS 0x8u          // menu backdrop blur
#define ABCUI_RB_CAP_OFFSCREEN_TARGETS 0x10u // viewport capture textures

// AbcUiRenderDraw::blend - semantic blend modes replace Renderer-private
// program-state bitmasks so a non-Renderer backend can implement them.
#define ABCUI_RB_BLEND_STRAIGHT_ALPHA 0      // SRC_ALPHA, ONE_MINUS_SRC_ALPHA
#define ABCUI_RB_BLEND_ADDITIVE 1            // SRC_ALPHA, ONE (kRenderTransAdd)
#define ABCUI_RB_BLEND_PREMULTIPLIED_ALPHA 2 // ONE, ONE_MINUS_SRC_ALPHA

typedef struct AbcUiRenderVertex {
  float position[2];
  float texcoord[2];
  float color[4];
} AbcUiRenderVertex;

typedef struct AbcUiRenderFilledVertex {
  float position[2];
  float color[4];
} AbcUiRenderFilledVertex;

typedef struct AbcUiRenderDraw {
  uint32_t structSize;
  const AbcUiRenderVertex *vertices;
  uint32_t vertexCount;
  const uint32_t *indices; // triangle list; count is a multiple of 3
  uint32_t indexCount;
  int32_t texture;     // engine texture id (VGUI2 channel namespace)
  int32_t maskTexture; // 0 = none; backend-defined masking otherwise
  int32_t blend;       // ABCUI_RB_BLEND_*
  const char *label;   // debug metadata; never null
} AbcUiRenderDraw;

typedef struct AbcUiRenderFilledDraw {
  uint32_t structSize;
  const AbcUiRenderFilledVertex *vertices;
  uint32_t vertexCount;
  const uint32_t *indices;
  uint32_t indexCount;
  int32_t blend;
  const char *label;
} AbcUiRenderFilledDraw;

// World pass (radar): render the world orthographically from the given
// camera into the backend-owned target, world class only. The framework
// saves/restores the draw cvars around the call.
typedef struct AbcUiWorldPassRequest {
  uint32_t structSize;
  int32_t width; // target size (square in practice)
  int32_t height;
  float origin[3];       // camera origin (world units)
  float angles[3];       // camera angles (90 pitch = straight down)
  float orthoHalfExtent; // visible span is 2 * halfExtent world units
  float orthoNear;
  float orthoFar;
  float clearColor[4];
} AbcUiWorldPassRequest;

// Blur pass (menu backdrop): capture `sourceTexture` (0 = the backend's
// final buffer) into an internally owned downscale chain of `width` x
// `height`, run `rounds` gaussian rounds and return the resulting texture
// id. Returns 0 when no source is available this frame.
typedef struct AbcUiBlurPassRequest {
  uint32_t structSize;
  int32_t width;
  int32_t height;
  uint32_t sourceTexture; // 0 = the backend's final buffer colour texture
  int32_t rounds;         // gaussian rounds; 0 keeps the raw capture
} AbcUiBlurPassRequest;

// Host -> backend frame beats. The backend calls these from its own frame
// source (Renderer callbacks, HUD_Redraw pump, ...). All entries except
// structSize/userdata may be null; the backend then skips that beat.
typedef struct AbcUiRenderFrameSink {
  uint32_t structSize;
  void *userdata;
  void(ABCUI_CALL *renderFrame)(void *userdata); // the UI 2D pass
  // Backend FBOs died (video restart / frame-buffer regeneration); owners
  // must drop texture ids and external registrations.
  void(ABCUI_CALL *invalidateTargets)(void *userdata);
  // The backend's world-render beat (Renderer: OnPreRenderView). Optional.
  void(ABCUI_CALL *worldPassBeat)(void *userdata);
} AbcUiRenderFrameSink;

// Host services. printf-style formatting happens host-side.
typedef struct AbcUiRenderBackendHost {
  uint32_t structSize;
  void(ABCUI_CALL *print)(const char *format, ...);
  void(ABCUI_CALL *warn)(const char *format, ...);
  void(ABCUI_CALL *error)(const char *format, ...);
  // The engine's screen size (GL backend surface source).
  void(ABCUI_CALL *engineScreenSize)(int32_t *width, int32_t *height);
  void *metahookApi;                               // metahook_api_t*
  void *(ABCUI_CALL *sysGetFactory)(void *module); // Sys_GetFactory
  // MetaHook plugin module handle by base file name (Renderer), or null.
  void *(ABCUI_CALL *pluginModule)(const char *baseName);
} AbcUiRenderBackendHost;

// Backend -> host descriptor. Optional entries must be null when the
// matching ABCUI_RB_CAP_* bit is clear; the loader validates exactly that.
typedef struct AbcUiRenderBackendDesc {
  uint32_t structSize;
  const char *id;          // cvar value, e.g. "renderer"; [a-z0-9_]+
  const char *displayName; // human-readable, ASCII
  uint32_t capabilities;   // ABCUI_RB_CAP_* bits
  void *userdata;          // passed to every callback below

  int32_t(ABCUI_CALL *initialize)(void *userdata,
                                  const AbcUiRenderBackendHost *host);
  void(ABCUI_CALL *shutdown)(void *userdata);
  int32_t(ABCUI_CALL *surfaceWidth)(void *userdata);
  int32_t(ABCUI_CALL *surfaceHeight)(void *userdata);
  void(ABCUI_CALL *installFrameSink)(void *userdata,
                                     const AbcUiRenderFrameSink *sink);
  void(ABCUI_CALL *removeFrameSink)(void *userdata);
  // REDRAW_BEAT backends render the sink here; RENDER_CALLBACKS backends
  // make it a no-op.
  void(ABCUI_CALL *pumpRedraw)(void *userdata, double seconds);
  void(ABCUI_CALL *beginFrame)(void *userdata); // bind final target
  void(ABCUI_CALL *endFrame)(void *userdata);
  void(ABCUI_CALL *set2D)(void *userdata, int32_t x, int32_t y, int32_t width,
                          int32_t height); // viewport == clip region
  void(ABCUI_CALL *finish2D)(void *userdata);
  void(ABCUI_CALL *pushTranslation)(void *userdata, float x, float y);
  void(ABCUI_CALL *popTranslation)(void *userdata);
  void(ABCUI_CALL *drawTextured)(void *userdata, const AbcUiRenderDraw *draw);
  void(ABCUI_CALL *drawFilled)(void *userdata,
                               const AbcUiRenderFilledDraw *draw);

  // --- WORLD_PASS ------------------------------------------------------
  uint32_t(ABCUI_CALL *worldEnsureTarget)(void *userdata, int32_t width,
                                          int32_t height); // texture id
  void(ABCUI_CALL *worldFreeTarget)(void *userdata);
  uint32_t(ABCUI_CALL *worldTexture)(void *userdata); // 0 = no target
  void(ABCUI_CALL *worldRender)(void *userdata,
                                const AbcUiWorldPassRequest *request);

  // --- BLUR_PASS -------------------------------------------------------
  uint32_t(ABCUI_CALL *blurRender)(void *userdata,
                                   const AbcUiBlurPassRequest *request);
  void(ABCUI_CALL *blurInvalidate)(void *userdata);

  // --- OFFSCREEN_TARGETS ------------------------------------------------
  // Viewport-capture textures for framework-side render passes (the
  // in-house model preview): the pass renders into the backend's current
  // framebuffer, clears its viewport region and copies the completed view
  // region out. The backend may capture the pre-composite scene to retain
  // alpha instead of the opaque final framebuffer, and it normalizes that
  // capture to straight-alpha RGBA8 so the framework can draw it with the
  // ordinary alpha blend.
  uint32_t(ABCUI_CALL *targetCreate)(void *userdata, int32_t width,
                                     int32_t height); // handle, 0 = fail
  void(ABCUI_CALL *targetFree)(void *userdata, uint32_t target);
  uint32_t(ABCUI_CALL *targetTexture)(void *userdata, uint32_t target);
  void(ABCUI_CALL *clearViewportRegion)(void *userdata, int32_t x, int32_t y,
                                        int32_t width, int32_t height,
                                        const float *rgba);
  void(ABCUI_CALL *copyViewportToTarget)(void *userdata, uint32_t target,
                                         int32_t x, int32_t y, int32_t width,
                                         int32_t height);
} AbcUiRenderBackendDesc;

#ifdef __cplusplus
} // extern "C"
#endif
