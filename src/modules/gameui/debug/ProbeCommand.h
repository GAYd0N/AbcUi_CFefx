#pragma once

// rmlui - abc_ui_probe / abc_ui_menu, Debug builds only.
//
// abc_ui_probe is a live diagnostic cross-section for acceptance and triage:
// dumps hudstate, per-document visibility and hardcoded panel element states
// (armor-bar-wrap, flashlight off-image), then opens a font-pipeline document.
// abc_ui_menu injects a canned netmsg::ShowMenu so drivers without a live
// server menu stream can exercise the TextMenu chain (open, slot-key gate,
// menuselect, re-show). abc_ui_cfefx injects canned netmsg::PopNum packages
// (hit / tier / score / sound / reset / status) so the cfefx damage loop is
// drivable without a server-side addon. The element ids and panel names these
// probe are deliberately listed only here, in a Debug translation unit - the
// production surface must not couple to them.

namespace rmlui {

// HUD_Init time (abc_ui_probe command). No-op in Release.
void registerProbeCommand();
void closeProbeDocument();

} // namespace rmlui
