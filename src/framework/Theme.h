#pragma once

// framework::Theme - HUD theme/stylesheet conventions and the hud_size /
// hud_alpha knobs. Plan 05 §4.3.
//
// RmlUi 6 attaches stylesheets through <link> tags in each document's <head>
// (no context-level sheet API in 6.3), so Theme's contract here is: every
// module document chooses its own stylesheets.
//
// Scale model (batch A): the old VGUI proportional system laid panels out in
// a 480-line design space (GetProportionalScaledValue = value * screenH/480),
// with fonts picked in resolution tiers. The Rml port maps design units 1:1
// onto `dp` by setting the context density ratio to screenH/480
// (applyScale), so .res coordinates carry over verbatim; font sizes in
// base.rcss use dp values matched to the old 1080p tier and follow the same
// proportional law at other resolutions.

namespace Rml {
class Context;
}

namespace framework {

namespace Theme {

// Context-level density ratio (screenH/480). Call from rmlui init and on
// every dimension change (HUD_VidInit chain).
void applyScale(Rml::Context *context);

// Current scale factor (screen height over 480); 1 when unscaled.
float scale();

} // namespace Theme

} // namespace framework
