// Extracted from GameUiModule.cpp (UTF-8). Responsibility: input/MenuInput.
#include "modules/gameui/GameUiModule.h"
#include "modsys/ApiTable.h"
#include <abc/Console.h>
#include <keydefs.h>
#include "rmlui/InputBridge.h"
#include "rmlui/TextInput.h"
namespace gameui {
void GameUiModule::startKeyCapture() {
  if (!BindModel.capturing() || CaptureToken.held())
    return;
#ifndef NDEBUG
  abc::ConsolePrint("[AbcUi] capture start row=%d\n", BindModel.captureRow());
#endif
  rmlui::setKeyCaptureClamp(true);
  CaptureToken = rmlui::uiRouter().acquire(
      &CaptureOwner, framework::uiinput::OwnerTier::kCapture);
  if (!CaptureToken.held()) {
    // Unrepresentable depth (8 owners): fail closed - no session.
    BindModel.cancelCapture();
    rmlui::setKeyCaptureClamp(false);
    abc::ConsoleWarn("[AbcUi] capture token unavailable (router full)\n");
  }
}

void GameUiModule::endKeyCapture() {
  CaptureToken.release();
  rmlui::setKeyCaptureClamp(false);
}

void GameUiModule::onCapturedKeyEdge(int keynum) {
  // Only the Debug trace reads the result.
  [[maybe_unused]] const auto result = BindModel.captureKey(keynum);
#ifndef NDEBUG
  abc::ConsolePrint("[AbcUi] capture key=%d result=%d conflict=%d\n", keynum,
                    static_cast<int>(result),
                    BindModel.conflictPending() ? 1 : 0);
#endif
  endKeyCapture(); // exactly one edge per session, whatever it was
  // The page re-projects from the published generation (the bind rows moved).
  framework::SettingsApi::bumpRevision();
}

void GameUiModule::onKeyCaptureForceLost() {
  BindModel.cancelCapture();
  rmlui::setKeyCaptureClamp(false);
}

bool GameUiModule::claimsKey([[maybe_unused]] int down,
                             [[maybe_unused]] int keynum) {
  return true;
}

bool GameUiModule::claimsWheel() {
  return true;
}

void GameUiModule::onClaimedKeyDown(int keynum) {
  // Exactly once per physical press (the router suppresses auto-repeat
  // here); the release pairs the just-fed ESC down inside the router.
  if (Controller.state().overlays.empty() && Command->handleKey(keynum))
    return;
  // Issue 10: the inventory window owns ESC/arrows/Enter/Delete while it is
  // up (its Esc closes the window and returns to gameplay, never the pause
  // page). Placed after the command menu: only the top page is visible, so
  // at most one of the two claims the key.
  if (Controller.state().overlays.empty() && Inventory->handleKey(keynum))
    return;
  if (keynum == '`' || keynum == '~') {
    // T16 console hotkey (the ` / toggleconsole key): while the menu
    // owns the keyboard the engine never sees this edge, so the
    // overlay toggle lives here. The key's own committed character
    // ("`"/"~" through the T07 text pipeline) is swallowed - the
    // toggle key never types (vanilla parity).
    if (!MenuPresented)
      return;
    rmlui::swallowToggleKeyText();
    // In-game hotkey close: when the console sits alone over the pause
    // page, one press closes the console AND returns to gameplay. On the
    // no-map home the menu must stay; deeper pages/modals keep theirs.
    const bool closeMenu = Controller.state().consoleHotkeyClosesMenu();
    onMenuAction("console");
    if (closeMenu) {
      Controller.dispatch(MenuEvent{EventKind::kResume});
      finishDispatch();
    }
    return;
  }
  if (keynum != K_ESCAPE || !MenuPresented)
    return;
  // T16: an ESC press the console page's own layers (context menu /
  // confirm pane / completion list) consumed (the Rml feed runs before
  // this action) closes only that layer - "同一事件不关闭两层". The claim
  // rides the sdk consolepage section.
  if (framework::ConsolePageApi::consumeEscapeClaim())
    return;
  // T07.b ESC priority: the first press cancels a running composition
  // instead of navigating; the next press toggles. The IME normally
  // swallows ESC itself, so this is the leak path.
  if (rmlui::textComposing()) {
    rmlui::cancelComposition();
    return;
  }
  // T12: an outstanding display confirmation is modal - ESC is consumed
  // (the dialog resolves by button or by its own deadline; a dismissal
  // cannot silently abandon an unconfirmed mode).
  if (VideoModel.awaitingConfirm())
    return;
  // The state machine owns the ESC semantics: pop one overlay, return
  // from a sub page, or close+resume on the pause home (T04 table).
  // The phase reconciles AFTER the toggle: a menu that survived a map
  // load (stale no-map phase) closes on the phase arrival instead of
  // re-opening as Pause from the cleared stack.
  // T10 exception: ESC on a dirty settings page is consumed by the
  // unsaved-changes prompt (second press dismisses it = stay) - the
  // controller's plain pop would silently discard the drafts.
  if (Controller.state().overlays.empty() &&
      Controller.state().pages.topIs(MenuPage::kServers) &&
      framework::ServerBrowserApi::consumeEscapeClaim())
    return;
  if (Controller.state().overlays.empty() &&
      Controller.state().pages.topIs(MenuPage::kSettings) &&
      framework::SettingsApi::consumeEscape()) {
    // The page owns the answer: a spray picker or the favorite menu in
    // flight closes on ESC, a dirty page raises the unsaved-changes prompt,
    // and a clean page reports "not consumed" so the menu pops it below.
    syncMenuPresentation();
    return;
  }
  Controller.dispatch(MenuEvent{EventKind::kMenuToggle});
  reconcilePhase();
  syncMenuPresentation();
  if (!Controller.isMenuActive())
    services::nativeUiBridge().requestMenuDown(
        services::NativeUiOrigin::kOwnUi);
}

void GameUiModule::onOwnershipLost() {
  // Force-release (focus loss / teardown): the bridge state must follow
  // so a later ESC cannot route into an empty router. The dismissal
  // clears the controller and the documents in one place.
  services::nativeUiBridge().requestMenuDown(services::NativeUiOrigin::kOwnUi);
}

} // namespace gameui
