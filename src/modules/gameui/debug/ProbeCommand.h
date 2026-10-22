#pragma once

// rmlui - abc_ui_probe / abc_ui_menu, Debug builds only.
//
// abc_ui_probe is a live diagnostic cross-section for acceptance and triage:
// dumps hudstate and per-document visibility, then opens a font-pipeline
// document.
// abc_ui_menu injects a canned netmsg::ShowMenu so drivers without a live
// server menu stream can exercise the TextMenu chain (open, slot-key gate,
// menuselect, re-show). The cfefx family is drivable through the module's own
// abc_ui_cfefx_* debug commands (modules/cfefx), which live with the panel;
// the element ids and panel names these probe are deliberately listed only
// here, in a Debug translation unit - the
// production surface must not couple to them.

namespace rmlui {

// HUD_Init time (abc_ui_probe command). No-op in Release.
void registerProbeCommand();
void closeProbeDocument();

} // namespace rmlui
