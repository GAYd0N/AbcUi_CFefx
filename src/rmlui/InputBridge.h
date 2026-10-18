#pragma once

// rmlui::InputBridge - game input -> Rml context. Plan 05 §3.5, T06 rework.
//
// ONE ownership truth: framework::uiinput::Router (tiered token stack,
// T06). The production router instance and its Sink live here (uiRouter());
// owners acquire/release through RAII tokens - the scoreboard pointer mode,
// the menu page, later console/capture tiers. rmlui::uiMouseOwner() reads
// router exclusivity for the engine-side gates (IN_Accumulate /
// IN_MouseEvent / CL_CreateMove in hooks.cpp).
//
// Event sources (T06 step-1 table; full pre/post + return semantics in
// execution/T06.md):
// - Keyboard / mouse keys / wheel: HUD_Key_Event export chain (hooks.cpp)
//   -> routeKey() is the single physical injection point.
// - Client IN_MouseEvent edges: fed directly while exclusive (the client
//   generates its own key edges there; the shared DeliveredDown dedup
//   state makes the overlap with the key stream idempotent).
// - Surface WndProc (VGUI2Extension BaseUI callback): mouse motion/buttons
//   plus focus-loss observation. The extension dispatches this callback
//   pre AND post (07 doc item 7) - every handler below is state-deduped or
//   idempotent, and nothing here ever consumes the message.
// - Async poll (HUD_Frame): position + button fallback for injected input
//   invisible to the WndProc chain (P6 acceptance finding).
// - BaseUI Key_Event callback: stays a no-op. The engine only routes keys
//   there while the original GameUI owns the key destination; wiring it is
//   T08's takeover seam. Choosing one physical injection entry IS the
//   dual-callback dedup; when T08 adds the second source it must land in
//   routeKey, not beside it.
//
// Mouse: the gate is the VGUI cursor visibility OR router exclusivity; the
// fallback poll (no VGUI2Extension) keeps the Phase 3 shape.

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Input.h>

#include "../framework/UiInputRouter.h"

namespace rmlui {

// Production router instance (ownership stack + keyboard state machine).
// The Sink side effects (Rml key feeds, engine mouse mode, opening-click
// suppression) are bound to it here.
framework::uiinput::Router &uiRouter();

// Kept for the engine-side gates and diagnostics: true while any owner
// holds UI input (scoreboard pointer mode, menu page, ...).
bool uiMouseOwner();
// Re-apply ownership AFTER native BaseUI operations (including its frame pump).
void maintainUiMouse();
// Client export gate: native UI must not reactivate gameplay mouse while owned.
// On handoff, discard UI motion before the next gameplay CreateMove.
void activateGameMouse();

// The single physical-event injection point (hooks.cpp H_HUD_Key_Event).
// Routes the edge through the ownership router (which feeds the UI what it
// orders), routes wheel pulses to onWheel when claimed, and returns the
// engine-consume decision (true = HUD_Key_Event returns 0). The pre-stage
// HUD consumers (vote answers, scoreboard pointer enter) run BEFORE this
// in hooks.cpp; `staticWheelClaim` carries their non-owning remainder
// (motd scroll).
bool routeKey(int down, int keynum, bool staticWheelClaim);

// T09: the SDL surface key stream (CallEngineSurfaceAppProc pre-dispatch)
// as a routeKey source for the menu-owned keyboard. While the suppression
// bridge holds the menu up, the engine stops delivering keys through the
// HUD_Key_Event chain entirely (verified live: bindings stay silent), so
// the surface stream is the only remaining physical source; this feeds it
// into the same routeKey injection point while menuUp && router exclusive.
// ESC is excluded - the engine window handler consumes it and its close
// decision arrives as the single routed close; a second feed would toggle
// twice per physical press. Wheel pulses ride the same claims as above.
void onSurfaceMenuKey(const void *sdlEvent);

// T11 capture clamp: while the key-binding capture session runs, every
// UI-fed edge is swallowed before the Rml dispatch (no scrolling, no button
// activation, no typing through a captured key). The router still runs its
// pairing and the capture owner still gets its once-per-press action -
// only the Rml feed is silenced. Enabling first releases held mouse buttons
// so no ghost press survives across the clamp window.
void setKeyCaptureClamp(bool clamp);
// Diagnostics (Debug command): the clamp's current state.
bool keyCaptureClampActive();
// Diagnostics: how many wheel pulses reached routeKey and how many were
// handed to Rml's ProcessMouseWheel (T11 live debugging).
void wheelDiagCounts(unsigned &pulses, unsigned &delivered);
// RmlUiModule::onWheel calls this right before the Context dispatch.
void InputBridge_noteWheelDelivered();

class InputBridge {
public:
  // HUD_Frame time, before Context::Update.
  static void update(Rml::Context *context);
  // Called once for a fresh context; seed its hover state on the next poll.
  static void resetPointerState();

  // Raw UI-side key feed (also the router Sink body): mouse keys ride the
  // shared button-dedup state, everything else maps through KeyMap into
  // ProcessKeyDown/Up. Returns the Rml consume flag (diagnostic only - no
  // consumption decision may read it, T06 step 3).
  static bool onKeyEvent(Rml::Context *context, int down, int keynum);

private:
  static void updateMouse(Rml::Context *context);
};

} // namespace rmlui
