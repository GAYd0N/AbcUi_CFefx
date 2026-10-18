// Extracted from GameUiModule.cpp (UTF-8). Responsibility:
// navigation/MenuPresentation.
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/GameUiPages.h"
#include <abc/Console.h>
#include "framework/Localization.h"
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

void GameUiModule::presentMenu() {
  if (!Shell || !Context || MenuPresented)
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
  if (MenuPresented) {
    MenuPresented = false;
    PageToken.release();
    services::nativeUiBridge().notePageHidden();
  }
  focusLog("dismissed");
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
  if (Shell)
    Shell->hide();
  if (SubStub)
    SubStub->hide();
  if (Console) {
    Console->hide();
    ConsolePresented = false;
  }
  if (Settings)
    Settings->hide();
  Server->hide();
  Create->hide();
  Player->hide();
  Vote->hide();
  Command->hide();
  if (ConfirmQuit)
    ConfirmQuit->hide();
  // T14 connection surfaces: hidden with the menu; a later present
  // re-projects from the state (the latches reset here).
  if (Loading)
    Loading->hide();
  if (ConnError)
    ConnError->hide();
  if (Password)
    Password->hide();
  if (DirectConnect)
    DirectConnect->hide();
  ErrorPresented = false;
  PasswordPresented = false;
  PasswordReprepare = false;
  DirectConnectPresented = false;
  LoadingVisible = false;
  // The video confirmation is NOT hidden by a generic hide: it must
  // stay up until confirmed/reverted (a dismissal cannot silently
  // abandon a mode the user has not confirmed). dismissMenu() calls
  // this, so keep it visibly module-owned.
  SettingsView->setPresented(false);
}

void GameUiModule::focusLog(const char *phase) {
#ifndef NDEBUG
  if (Context) {
    if (Rml::Element *focus = Context->GetFocusElement())
      abc::ConsolePrint("[AbcUi] menu %s, focus on '%s'\n", phase,
                        focus->GetId().c_str());
    else
      abc::ConsoleWarn("[AbcUi] menu %s, no focus element\n", phase);
  }
#else
  (void)phase;
#endif
}

void GameUiModule::showStubPage(panels::menushared::MenuStubPage *stub,
                                MenuPage page, bool console) {
  if (!stub)
    return;
  if (!console && LastStubPage == page && stub->isVisible())
    return; // same surface already presented: no text churn
  LastStubPage = page;
  using Resolve = framework::Localization;
  std::string title, body;
  const char *titleToken = "#MainMenu_StubTitle";
  const char *bodyToken = "#MainMenu_StubSettings";
  switch (page) {
  case MenuPage::kLoading:
    bodyToken = "#MainMenu_StubLoading";
    break;
  case MenuPage::kMain:
    bodyToken = "#MainMenu_StubConsole";
    break;
  default:
    break; // settings: the default tokens
  }
  Resolve::translate(title, titleToken + 1);
  Resolve::translate(body, bodyToken + 1);
  stub->showFor(title.c_str(), body.c_str());
  stub->show();
}

} // namespace gameui
