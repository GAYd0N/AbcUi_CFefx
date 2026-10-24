// rmlui::PaintGate implementation - see PaintGate.h.
#include <metahook.h>

#include "PaintGate.h"

#include <IPanel.h>

#include <abc/Console.h>
#include <abc/GameData.h>
#include <abc/Plugin.h>

#include <abcui/events.h>

#include "../modsys/EventBus.h"
#include "../services/GameUiBridge.h"

#include <Windows.h>
#include <cstring>

namespace rmlui {

namespace {

// Install-time-captured handles. Guarded by Installed: the two hooks, the
// IPanel pointer and the originals are one atomic bundle (T08 step 7 -
// partial installs are undone before failure is reported).
bool Installed = false;
vgui::IPanel *Panel = nullptr;
void(__fastcall *OriginalPaintTraverse)(void *, int, vgui::VPANEL, bool,
                                        bool) = nullptr;
vgui::VPANEL(__fastcall *OriginalIsWithinTraverse)(void *, int, vgui::VPANEL,
                                                   int, int, bool) = nullptr;
hook_t *TraverseHook = nullptr;
hook_t *HitTestHook = nullptr;

// Clear the keyboard/mouse input flags of one native panel and its whole
// subtree. Conditional writes only: after the first pass every flag already
// reads false, so steady state is pure reads (no vgui state churn per frame).
// Works on the module-identified entry because GameUI content is exactly the
// panels the module rule names - the engine roots and the client module never
// enter here.
void clearNativeInput(vgui::VPANEL panel, uint32_t &cleared) {
  if (!Panel || !panel)
    return;
  if (Panel->IsKeyBoardInputEnabled(panel)) {
    Panel->SetKeyBoardInputEnabled(panel, false);
    ++cleared;
  }
  if (Panel->IsMouseInputEnabled(panel)) {
    Panel->SetMouseInputEnabled(panel, false);
    ++cleared;
  }
  const int children = Panel->GetChildCount(panel);
  for (int i = 0; i < children; i++)
    clearNativeInput(Panel->GetChild(panel, i), cleared);
}

bool childNamed(vgui::VPANEL parent, int children, const char *name) {
  for (int i = 0; i < children; ++i) {
    const char *child = Panel->GetName(Panel->GetChild(parent, i));
    if (child && std::strcmp(child, name) == 0)
      return true;
  }
  return false;
}

// The client viewport's observer bottom bar. It is not in the gHUD chain and
// has no gamedata symbol; its three labels keep the constructor names across
// client builds (SpectatorPanel::Initialize), so the child names are the
// stable identity. Checked per traversal: the panel is recreated on a client
// VidInit, and a cached VPANEL would then be stale.
bool nativeSpectatorPanel(vgui::VPANEL panel) {
  if (!Panel || !panel)
    return false;
  const int children = Panel->GetChildCount(panel);
  if (children != 5)
    return false;
  return childNamed(panel, children, "Health: 100") &&
         childNamed(panel, children, "Armor: 100") &&
         childNamed(panel, children, "Spectator Bottom");
}

// The selective paint filter. No IsVisible/IsPopup reads (the original's own
// decision logic stays untouched for everything allowed); the module string
// is the whole rule and must match the install-reported identities exactly
// (T02 §6: "GameUI" / "ServerBrowser"; never "GAMEUIDLL").
void __fastcall H_PaintTraverse(void *pthis, int dummy, vgui::VPANEL vguiPanel,
                                bool forceRepaint, bool allowForce) {
  // The observer overlay is a client VGUI child, not a GameUI module panel:
  // it yields to the same per-element claim as the gHUD list (the module
  // returns zero while its replacement document is unavailable).
  if (nativeSpectatorPanel(vguiPanel)) {
    const AbcUiNativeHudEvent event{ABCUI_NATIVE_HUD_SPECTATOR};
    if (framework::eventBus().publishVeto(ABCUI_CHANNEL_HUD_NATIVE_DRAW,
                                          event))
      return;
  }
  const char *module = Panel ? Panel->GetModuleName(vguiPanel) : nullptr;
  if (services::nativeUiBridge().gatePaintAttempt(module)) {
    uint32_t cleared = 0;
    clearNativeInput(vguiPanel, cleared);
    if (cleared)
      services::nativeUiBridge().noteInputFlagsCleared(cleared);
    return;
  }
  if (OriginalPaintTraverse)
    OriginalPaintTraverse(pthis, dummy, vguiPanel, forceRepaint, allowForce);
}

// The mouse hit-test filter: a native module panel reports "nothing here".
vgui::VPANEL __fastcall H_IsWithinTraverse(void *pthis, int dummy,
                                           vgui::VPANEL vguiPanel, int x, int y,
                                           bool traversePopups) {
  const char *module = Panel ? Panel->GetModuleName(vguiPanel) : nullptr;
  if (services::nativeUiBridge().gateHitTestAttempt(module))
    return 0;
  if (OriginalIsWithinTraverse)
    return OriginalIsWithinTraverse(pthis, dummy, vguiPanel, x, y,
                                    traversePopups);
  return 0;
}

// Anything that can be undone without the hook objects (pre-hook failures).
void failCleanup() {
  Panel = nullptr;
  OriginalPaintTraverse = nullptr;
  OriginalIsWithinTraverse = nullptr;
}

} // namespace

void installPaintGate() {
  if (Installed)
    return;

  // Step 1: the vgui2 module and its image range. The slot identity checks
  // below need both; a missing module is fatal (the strict seam cannot be
  // established without it).
  const HMODULE vgui2 = GetModuleHandleA("vgui2.dll");
  if (!vgui2)
    abc::SysError("[AbcUi] paint gate: vgui2.dll not loaded; cannot establish "
                  "native UI suppression");
  const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(vgui2);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    abc::SysError("[AbcUi] paint gate: vgui2.dll image header invalid");
  const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(
      reinterpret_cast<const BYTE *>(vgui2) + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE)
    abc::SysError("[AbcUi] paint gate: vgui2.dll NT header invalid");
  const auto imageBase = reinterpret_cast<const BYTE *>(vgui2);
  const auto imageEnd = imageBase + nt->OptionalHeader.SizeOfImage;

  // Step 2: the IPanel interface through the vgui2 factory.
  if (!g_pMetaHookAPI)
    abc::SysError("[AbcUi] paint gate: metahook API unavailable");
  CreateInterfaceFn getFactory =
      Sys_GetFactory(reinterpret_cast<HINTERFACEMODULE>(vgui2));
  if (!getFactory)
    abc::SysError("[AbcUi] paint gate: vgui2.dll has no factory");
  auto *panel = static_cast<vgui::IPanel *>(
      getFactory(VGUI_PANEL_INTERFACE_VERSION, nullptr));
  if (!panel) {
    failCleanup();
    abc::SysError("[AbcUi] paint gate: \"%s\" unavailable from vgui2.dll",
                  VGUI_PANEL_INTERFACE_VERSION);
  }

  // Step 3: query the binary-matched slots before validating their owners.
  // MetaHook omits slot-only virtualFunction declarations from its runtime
  // catalog; the local scalar preserves the published PaintTraverse index.
  const auto paintIndex =
      abc::GameScalar(vgui2, "AbcUi_IPanel_PaintTraverse_index");
  const auto hitTestIndex =
      abc::GameScalar(vgui2, "AbcUi_IPanel_IsWithinTraverse_index");
  if (paintIndex > 255 || hitTestIndex > 255)
    abc::SysError("[AbcUi] paint gate: invalid gamedata virtual slot");
  void **vtable = *reinterpret_cast<void ***>(panel);
  const void *paintEntry = vtable[paintIndex];
  const void *hitTestEntry = vtable[hitTestIndex];
  if (paintEntry < imageBase || paintEntry >= imageEnd) {
    failCleanup();
    abc::SysError("[AbcUi] paint gate: IPanel PaintTraverse (%p) outside the "
                  "vgui2 image [%p..%p); layout drifted",
                  paintEntry, imageBase, imageEnd);
  }
  if (hitTestEntry < imageBase || hitTestEntry >= imageEnd) {
    failCleanup();
    abc::SysError("[AbcUi] paint gate: IPanel IsWithinTraverse (%p) outside "
                  "the vgui2 image; layout drifted",
                  hitTestEntry, imageBase, imageEnd);
  }

  // Step 4: install both hooks. The bridge stays unarmed until this
  // succeeded, so a failed install cannot have suppressed anything yet.
  void *originalTraverse = nullptr;
  hook_t *traverseHook = g_pMetaHookAPI->VFTHook(
      panel, 0, paintIndex, reinterpret_cast<void *>(&H_PaintTraverse),
      &originalTraverse);
  void *originalHitTest = nullptr;
  hook_t *hitTestHook =
      traverseHook
          ? g_pMetaHookAPI->VFTHook(
                panel, 0, hitTestIndex,
                reinterpret_cast<void *>(&H_IsWithinTraverse), &originalHitTest)
          : nullptr;
  if (!traverseHook || !originalTraverse || !hitTestHook || !originalHitTest) {
    if (traverseHook)
      g_pMetaHookAPI->UnHook(traverseHook);
    if (hitTestHook)
      g_pMetaHookAPI->UnHook(hitTestHook);
    failCleanup();
    abc::SysError(
        "[AbcUi] paint gate: VFTHook of IPanel gamedata slots failed");
  }

  Panel = panel;
  OriginalPaintTraverse =
      reinterpret_cast<decltype(OriginalPaintTraverse)>(originalTraverse);
  OriginalIsWithinTraverse =
      reinterpret_cast<decltype(OriginalIsWithinTraverse)>(originalHitTest);
  TraverseHook = traverseHook;
  HitTestHook = hitTestHook;
  Installed = true;
  services::nativeUiBridge().arm();
  abc::ConsolePrint("[AbcUi] paint gate: installed (IPanel gamedata slots; "
                    "suppression requires module takeover)\n");
}

void uninstallPaintGate() {
  // The bridge disarms only here: the strict takeover keeps suppressing
  // until the original UI can no longer paint (ClientVGUI::Shutdown is
  // the primary seam; GameUI::Shutdown/teardown are fallbacks, §9). Everything
  // below runs on the main thread inside that callback.
  services::nativeUiBridge().disarm();
  if (!Installed)
    return;
  // UnHook only reports "found" - at teardown MetaHook may already have
  // freed the hook records (MH_FreeAllHook restores the same vtable
  // entries), and the pointer comparison inside UnHook is safe for that.
  if (TraverseHook) {
    g_pMetaHookAPI->UnHook(TraverseHook);
    TraverseHook = nullptr;
  }
  if (HitTestHook) {
    g_pMetaHookAPI->UnHook(HitTestHook);
    HitTestHook = nullptr;
  }
  Installed = false;
  Panel = nullptr;
  OriginalPaintTraverse = nullptr;
  OriginalIsWithinTraverse = nullptr;
  abc::ConsolePrint("[AbcUi] paint gate: uninstalled\n");
}

bool paintGateInstalled() {
  return Installed;
}

} // namespace rmlui
