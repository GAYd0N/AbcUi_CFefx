// Extracted from GameUiModule.cpp (UTF-8). Responsibility:
// navigation/MenuPresentation.
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include "modules/gameui/GameUiModule.h"
#include <abc/Console.h>
#include "framework/Localization.h"
#include "modsys/ModuleDocuments.h"
#include "rmlui/InputBridge.h"
#include <string>
#include "modules/gameui/engine/MenuBindings.h"
namespace gameui {
using namespace engine;
void GameUiModule::onNativeMenuShow() {
  presentMenu();
}

void GameUiModule::onNativeMenuHide() {
  dismissMenu();
}

void GameUiModule::onNativeConsoleOpen() {
  if (!Controller.state().overlays.contains(MenuOverlay::kConsole))
    Controller.dispatch(MenuEvent{EventKind::kConsoleToggle});
  if (MenuPresented)
    syncMenuPresentation();
}

void GameUiModule::onNativeConsoleClose() {
  if (Controller.state().overlays.contains(MenuOverlay::kConsole))
    Controller.dispatch(MenuEvent{EventKind::kConsoleToggle});
  if (MenuPresented)
    syncMenuPresentation();
}

// The home pages are module documents; Rml assembly (the context) is the
// readiness fact the takeover and every page flow's shellReady seam use.
void GameUiModule::presentMenu() {
  if (!Context || MenuPresented || !services::nativeUiBridge().armed())
    return;
  MenuPresented = true;
  // Real ownership token (T06): the menu claims keys/wheel; mouse
  // reachability rides the router exclusivity like the pointer mode.
  PageToken =
      rmlui::uiRouter().acquire(this, framework::uiinput::OwnerTier::kMenu);
  if (!PageToken.held())
    abc::ConsoleWarn(
        "[AbcUi] menu ownership token unavailable (router full)\n");
  services::nativeUiBridge().notePageShown();
  // The state machine follows the bridge (idempotent open), then the
  // presentation follows the state machine. The phase reconciles first,
  // so the open lands on the right home (Pause in-game, Main no-map).
  reconcilePhase();
  Controller.dispatch(MenuEvent{EventKind::kMenuShown});
  syncMenuPresentation();
  if (Controller.state().phase == gameui::menu::BackendPhase::kInGame)
    menuBackend().menuShownInGame(); // singleplayer pause parity
  focusLog("presented");
}

void GameUiModule::dismissMenu() {
  // The bridge lowered the menu from outside the state machine's
  // decision (routed ESC close, engine hide, focus loss): force-clear
  // the controller, hide the documents, release ownership. No unpause
  // here - the simulation only resumes on a decided close
  // (EngineUiBackend::resumeGame), never on a force-release.
  // A running capture session ends with it (draft-free, clamp off).
  endKeyCapture();
  BindModel.cancelCapture();
  Command->cancelPending();
  // T13b: menu focus loss ends the mic test with it (the device
  // session must never outlive the surface that owns it).
  VoiceModel.stopTest();
  Controller.dispatch(MenuEvent{EventKind::kMenuHidden});
  hideAllMenuDocuments();
  // Issue #5: the menu takes the console layer with it - vanilla
  // CGameUI::HideGameUI issued `hideconsole` here. Lowering only our own
  // presentation truth left the ENGINE's console flag set, so the next
  // ActivateGameUI re-entered the console. The bridge truth is already down
  // (requestMenuDown) and hideConsole() re-asserts it before the engine
  // call, so the proxied IGameConsole::Hide echo cannot toggle twice.
  menuBackend().hideConsole();
  if (MenuPresented) {
    MenuPresented = false;
    PageToken.release();
    services::nativeUiBridge().notePageHidden();
  }
  focusLog("dismissed");
  publishMenuPresentation();
}

void GameUiModule::reconcilePhase() {
  const auto current = Controller.state().phase;
  if (current != gameui::menu::BackendPhase::kBooting &&
      current != gameui::menu::BackendPhase::kInGame &&
      current != gameui::menu::BackendPhase::kDisconnected)
    return; // connecting/loading/fatal are T14's to reconcile
  const auto target = services::nativeUiBridge().hasLevel()
                          ? gameui::menu::BackendPhase::kInGame
                          : gameui::menu::BackendPhase::kDisconnected;
  if (current == target)
    return;
  Controller.dispatch(
      gameui::menu::makePhaseChanged(NextPhaseSession++, target));
}

void GameUiModule::hideAllMenuDocuments() {
  // Every menu surface joins the layer (ABCUI_DOC_FLAG_MENU): the framework
  // hides them with the menu, so no page can outlive a lowered menu.
  framework::module_documents::hideMenu();
  Server->hide();
  // T14 connection surfaces: module documents hidden with the menu (the
  // ABCUI_DOC_FLAG_MENU layer); a later present re-projects from the
  // state.
  // The display confirmation is NOT hidden by a generic hide: its document
  // carries no menu flag, so it stays up until confirmed/reverted (a
  // dismissal cannot silently abandon a mode the user has not confirmed).
}

void GameUiModule::focusLog([[maybe_unused]] const char *phase) {
#ifndef NDEBUG
  if (Context) {
    if (Rml::Element *focus = Context->GetFocusElement())
      abc::ConsolePrint("[AbcUi] menu %s, focus on '%s'\n", phase,
                        focus->GetId().c_str());
    else
      abc::ConsoleWarn("[AbcUi] menu %s, no focus element\n", phase);
  }
#endif
}

// --- menu section backing (services::menu::Host) ---------------------------
// The pages are modules: they act through the menu section and learn the
// settled state from menu.state. These four entry points are the exact
// orchestrations the page flows used to reach through host lambdas
// (ensure the menu, reconcile the phase, dispatch the intent, finish).

services::menu::Snapshot GameUiModule::menuSnapshot() const {
  services::menu::Snapshot snapshot;
  const menu::MenuState &state = Controller.state();
  snapshot.phase = static_cast<int32_t>(state.phase);
  snapshot.page = state.pages.empty() ? ABCUI_MENU_PAGE_NONE
                                      : static_cast<int32_t>(state.pages.top());
  snapshot.up = MenuPresented ? 1 : 0;
  snapshot.loading = (state.phase == menu::BackendPhase::kConnecting ||
                      state.phase == menu::BackendPhase::kLoading)
                         ? 1
                         : 0;
  snapshot.consoleOpen = state.overlays.contains(MenuOverlay::kConsole) ? 1 : 0;
  snapshot.hasLevel = services::nativeUiBridge().hasLevel() ? 1 : 0;
  snapshot.canClose = state.canCloseMenu() ? 1 : 0;
  snapshot.generation = state.connectGeneration;
  if (state.overlays.contains(MenuOverlay::kConsole))
    snapshot.overlays |= ABCUI_MENU_OVERLAY_CONSOLE;
  if (state.overlays.contains(MenuOverlay::kConfirmQuit))
    snapshot.overlays |= ABCUI_MENU_OVERLAY_CONFIRM_QUIT;
  if (state.overlays.contains(MenuOverlay::kPassword))
    snapshot.overlays |= ABCUI_MENU_OVERLAY_PASSWORD;
  if (state.overlays.contains(MenuOverlay::kConnectionError))
    snapshot.overlays |= ABCUI_MENU_OVERLAY_CONNECTION_ERROR;
  if (state.overlays.contains(MenuOverlay::kDirectConnect))
    snapshot.overlays |= ABCUI_MENU_OVERLAY_DIRECT_CONNECT;
  switch (state.attemptKind) {
  case menu::AttemptKind::kConnect:
    snapshot.attemptKind = ABCUI_MENU_ATTEMPT_CONNECT;
    break;
  case menu::AttemptKind::kLocalGame:
    snapshot.attemptKind = ABCUI_MENU_ATTEMPT_LOCAL_GAME;
    break;
  case menu::AttemptKind::kMapLoad:
    snapshot.attemptKind = ABCUI_MENU_ATTEMPT_MAP_LOAD;
    break;
  default:
    snapshot.attemptKind = ABCUI_MENU_ATTEMPT_NONE;
    break;
  }
  return snapshot;
}

int32_t GameUiModule::ensureMenuUp() {
  if (!Context || !services::nativeUiBridge().armed())
    return 0; // the command landed before Rml assembly
  if (!MenuPresented) {
    if (!services::nativeUiBridge().menuUp())
      services::nativeUiBridge().requestMenuUp(
          services::NativeUiOrigin::kOwnUi);
    if (!MenuPresented)
      presentMenu();
  }
  return MenuPresented ? 1 : 0;
}

int32_t GameUiModule::openMenuPage(int32_t page) {
  if (page < ABCUI_MENU_PAGE_MAIN || page > ABCUI_MENU_PAGE_LOADING)
    return 0;
  const MenuPage target = static_cast<MenuPage>(page);
  if (!ensureMenuUp())
    return 0;
  reconcilePhase();
  // Sub pages are legal on the no-map home screen too: the shell offers the
  // server browser, create game and settings entries there, and the
  // controller's own openPage already refuses only the phases that must hold
  // input (Connecting/Loading/Fatal). A kInGame-only gate here contradicted
  // that home menu and left every entry except Quit inert.
  Controller.dispatch(menu::makeOpenPage(target));
  finishDispatch();
  return Controller.state().pages.topIs(target) ? 1 : 0;
}

void GameUiModule::menuBack() {
  Controller.dispatch(MenuEvent{EventKind::kBack});
  finishDispatch();
}

void GameUiModule::menuQuit() {
  // The confirmation overlay is the controller's decision; a page module
  // presents its own document once the overlay is on the stack.
  Controller.dispatch(MenuEvent{EventKind::kQuitRequested});
  finishDispatch();
}

void GameUiModule::menuQuitResolve(int32_t confirmed) {
  Controller.dispatch(menu::makeQuitResolved(confirmed != 0));
  finishDispatch();
}

void GameUiModule::menuDisconnect() {
  Controller.dispatch(MenuEvent{EventKind::kDisconnectRequested});
  finishDispatch();
}

void GameUiModule::menuToggleConsole() {
  // Same shape the shell's console entry uses: close is idempotent and never
  // reopens, otherwise open through the bridge (which raises the menu when
  // needed and notifies the sink).
  services::NativeUiBridge &bridge = services::nativeUiBridge();
  if (bridge.consoleRequested())
    bridge.requestConsoleClose(services::NativeUiOrigin::kOwnUi);
  else
    bridge.requestConsoleOpen(services::NativeUiOrigin::kOwnUi);
  finishDispatch();
}

int32_t GameUiModule::resumeFromMenu() {
  if (Controller.state().phase != menu::BackendPhase::kInGame)
    return 0;
  Controller.dispatch(MenuEvent{EventKind::kResume});
  finishDispatch();
  return Controller.isMenuActive() ? 0 : 1;
}

} // namespace gameui
