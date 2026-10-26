#pragma once

// rmlui - RmlUi assembly module. Plan 05 §2/§3.
//
// Startup order (LoadClient): abc::render::boot (caller - the render-backend
// DI root, issue #24) -> vgui2ext import -> Rml::Initialise (render/system/
// file interfaces) -> context -> fonts -> theme assets -> panels -> debugger
// -> backend->installFrameSink.
//
// Render timing: the backend's frame beat drives the Rml render pass through
// the IFrameSink this module installs. The Renderer backend fires it from
// IMetaRendererCallbacks::OnRenderEndFrame - exactly once per frame
// (GL_EndRendering -> R_RenderEndFrame), after all built-in post-processing
// and immediately before the final buffer is blitted to screen. This is the
// "implementation micro-adjustment" the plan anticipated vs OnPostRenderView:
// the latter fires per view render (water portals/mirrors would double-draw
// the UI). A RedrawBeat backend (the future GL backend) pumps the same sink
// from HUD_Redraw instead.

#include <functional>

namespace Rml {
class Context;
}

namespace rmlui {

class AbcRenderInterface;

// Lifecycle callbacks supplied by the composition root (GameUiModule). The
// RmlUi layer owns the context, but it must not include modules/Module.h and
// walk the module registry itself: that made rmlui depend on the top layer.
struct LifecycleSink {
  std::function<void(Rml::Context *)> contextReady;
  std::function<void()> shutdown;
  std::function<void(double)> frame;
  std::function<void()> vidInit;
};
void SetLifecycleSink(LifecycleSink sink);

// Deferred assembly: the first HUD_Frame builds the Rml context (one-shot).
// LoadClient only grabs the renderer/VGUI2 interfaces (both proven safe
// there); the context itself cannot be built at LoadClient - the VGUI
// surface texture channel the font engine needs is not usable that early,
// and with -windowed the chained HUD_VidInit fires before the export patch
// lands (both Phase 3 trace findings).
void init();
// Idempotent; safe from both HUD_Shutdown and ExitGame.
void shutdown();

// HUD_Init time: abc_ui_debugger command (cvar/command red line, plan 08 §4.9).
void registerCommands();

// Live Rml context, null before init()/after shutdown() - Debug probe
// tooling only (ProbeCommand.cpp).
Rml::Context *context();

// Live render interface, null before init()/after shutdown(). The radar
// panel registers its FBO as an external masked texture through it (batch D).
AbcRenderInterface *renderInterface();

// T21: how many font faces the last init() loaded (0 means text cannot render
// anywhere). Surfaced for the fault diagnostics; the 0 case is a fatal exit
// inside init() itself (strict takeover - see RmlUiModule.cpp).
int loadedFontCount();

#ifndef NDEBUG
// Debug per-frame phase timer (abc_ui_perf, Phase 8 benchmark instrument):
// QueryPerformanceCounter brackets around the two Context passes - Update()
// per HUD_Frame and onRedraw, Render() per OnRenderEndFrame. Accumulates a
// rolling window; the command prints avg/max over the window and resets.
struct PerfSnapshot {
  int Frames = 0;
  double UpdateAvgMs = 0.0, UpdateMaxMs = 0.0;
  double RenderAvgMs = 0.0, RenderMaxMs = 0.0;
};
PerfSnapshot perfSnapshot();
#endif

// Spine entries (hooks.cpp).
void onFrame(double frameTime);
void onDisconnected();
// HUD_Redraw chain: projection consumers reposition + second Update.
void onRedraw(double frameTime);
void onVidInit();
void onKeyEvent(int down, int keynum);
// Engine wheel pulses (HUD_Key_Event keynums) -> Context::ProcessMouseWheel;
// called while a scrollable UI consumer owns the wheel.
void onWheel(int down, int keynum);
// Engine surface WndProc messages (vgui2ext BaseUI callback, R-4 batch C):
// UI-level mouse motion/buttons for the Rml context, plus focus-loss
// observation (T06). Observation-only - the message result is never set.
// The hwnd operand feeds the T07 IME window cache.
void onSurfaceWndProc(const void *hwnd, unsigned int msg, unsigned int wparam,
                      long lparam);
// True while any owner holds UI input (rmlui::uiRouter exclusivity): the
// engine-side gates in hooks.cpp read this (T06 ownership stack).
bool uiMouseOwner();
// HUD_Shutdown: full teardown (context/docs die with the client state).
void onHudShutdown();

} // namespace rmlui
