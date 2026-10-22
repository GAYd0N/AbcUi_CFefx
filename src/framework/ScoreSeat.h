#pragma once

// framework::scoreseat - the scoreboard's interaction seat.
//
// The scoreboard table is a UI module, but three of its seams belong to the
// framework spine: the +/-showscores latch that CL_CreateMove turns into
// usercmd IN_SCORE, the HUD_Redraw intermission flag, and pointer mode - the
// interactive-HUD input tier (MOUSE2 unlocks the cursor, the router token
// gates Rml mouse input and the client mouse/view hooks).
//
// The event channels this service publishes:
//   hud.intermission  (AbcUiBoolEvent) - intermission entered/left
//   hud.scorePointer  (AbcUiBoolEvent) - pointer mode acquired/released
//
// This service owns those three facts so the spine never names a panel:
// hooks.cpp calls setIntermission/onKeyEvent/held, and the module reports
// whether its table is on screen (setVisible) and whether it holds the
// scores key (setHeld). Pointer mode publishes hud.scorePointer so the
// module can close its context menu on a force-release.

namespace framework {

namespace scoreseat {

// HUD_Redraw: intermission edge (the old SetInterMission ShowPanel rule).
void setIntermission(bool intermission);
bool intermission();

// The module's +/-showscores subscription reports the button state; this is
// what CL_CreateMove turns into IN_SCORE (the server streams ping/loss only
// to clients it believes hold the scoreboard).
void setHeld(bool held);
bool held();

// The module's table is on screen (pointer mode needs it).
void setVisible(bool visible);
bool visible();

// Interactive-HUD pointer tier: acquires/releases the router token and the
// native cursor. Refused while the table is not visible.
void setPointerMode(bool enabled);
bool pointerMode();
bool wantsWheel();

// H_HUD_Key_Event seam: true = the key served the scoreboard and must not
// reach the engine bindings.
bool onKeyEvent(int down, int keynum);

} // namespace scoreseat

} // namespace framework
