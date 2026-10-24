#pragma once

// rmlui::PaintGate - the selective native-UI paint/input gate (T08 steps 1/4).
//
// The candidate seam (T02 §7; complete popup coverage still needs live proof):
// VFT-hook IPanel (vgui2.dll, "VGUI_Panel007") and drop every traversal whose
// IPanel::GetModuleName is a native-UI module ("GameUI", "ServerBrowser" -
// the exact strings this install reports, T02 §6), plus the client viewport's
// observer bottom bar when a module claims it on hud.native.draw. The module
// string is read per traversal, so panel creation/destruction/recreation needs
// no cache and no VPANEL identity; the observer panel is identified by its
// constructor-stable child label names, also per traversal.
//
// Two VFT slots are taken (both validated to point into the vgui2 image):
//   - slot 41 PaintTraverse: blocked entries are swallowed (the original is
//     never called for that subtree), and the native panel's whole subtree
//     gets its keyboard/mouse input flags cleared (交互隔离: the old UI must
//     not be able to hold focus or capture, and hidden-but-clickable panels
//     are exactly what a paint-only filter leaves behind). The flag clear is
//     conditional, so steady state performs no writes.
//   - slot 43 IsWithinTraverse: mouse hit tests against a native module panel
//     report "nothing here" - the old dialog/taskbar can never be the
//     mouse-over target even if a future panel re-enables its input flag.
//
// Install timing: from the BaseUI::Initialize callback (VGUI2Extension),
// which the T02 timeline puts ~2s before the first native paint; the
// extension has finished its own hook installation by then. The bridge is
// armed only after every hook is in place - a half-installed gate must not
// half-suppress.
//
// Failure policy (T08 step 7, hook-contract §8): any step that cannot be
// verified - vgui2 missing, VGUI_Panel007 unavailable, a slot not pointing
// into the vgui2 image, VFTHook refused - undoes whatever was installed and
// stops the process via abc::SysError. The strict takeover never falls back
// to the original UI.
//
// Uninstall timing: from the GameUI::Shutdown notification, before the
// original CGameUI::Shutdown unloads GameUI.dll/vgui2.dll (hook-contract
// §9). BaseUI::Shutdown is too late - by then vgui2 is gone and the hooks
// must not be touched. Until then the bridge stays armed: once the original
// UI can no longer draw, suppression may lift.

namespace rmlui {

// Idempotent; SysError (explicit stop) on any unverifiable step.
void installPaintGate();
// Unhooks if installed and drops the bridge back to unarmed. Safe to call
// when not installed (no-op), and safe at process teardown (MetaHook's own
// free path may already have removed the hooks; UnHook then just reports it
// did not find them).
void uninstallPaintGate();

// Diagnostics (debug command + acceptance).
bool paintGateInstalled();

} // namespace rmlui
