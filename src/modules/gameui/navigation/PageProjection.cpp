// Extracted from GameUiModule.cpp (UTF-8). Responsibility:
// navigation/PageProjection.
#include "modules/gameui/GameUiModule.h"
#include <abc/Plugin.h>
#include "services/settings/SettingsPageService.h"
#include "modules/gameui/engine/MenuBindings.h"
namespace gameui {
using namespace engine;
void GameUiModule::projectMenuPresentation() {
  if (!Context)
    return;
  const auto &state = Controller.state();
  if (!Controller.isMenuActive()) {
    hideAllMenuDocuments();
    if (ServerBrowser.Open || ServerBrowser.Query.listRequest() != 0)
      Server->releaseQueries();
    return;
  }
  // The menu home (kMain/kPause) is a module document (abcui.gameui): it
  // presents its section from menu.state and projects the entry conditions,
  // the character card, the footer and the friends board from the sdk
  // menuconfig/social sections.

  // Project each implemented page from navigation state. Every current page
  // is a module document; the switch only routes the framework-owned flows
  // (the server query, the diagnostic beats).
  if (!state.pages.empty()) {
    switch (state.pages.top()) {
    case MenuPage::kMain:
    case MenuPage::kPause:
      // The page is a module document: it presents itself from menu.state
      // once the page is on top (the entry focus defers to overlays).
      break;
    case MenuPage::kSettings:
      // The settings page is a module document (abcui.gameui): it presents
      // itself from menu.state and projects from the sdk settings section.
      break;
    case MenuPage::kServers:
      // T18: the real browser replaced the T09 stub. tick() runs on
      // the frame beat; the fresh-open query trigger is the T17
      // block below.
      Server->present();
      break;
    case MenuPage::kCreateGame:
      // The page is a module document (abcui.gameui): it presents itself
      // from menu.state once the page is on top.
      break;
    case MenuPage::kPlayerList:
      // The page is a module document (abcui.gameui): it presents itself
      // from menu.state once the page is on top.
      break;
    case MenuPage::kVoteMenu:
      // The page is a module document (abcui.gameui): it presents itself
      // from menu.state once the page is on top.
      break;
    case MenuPage::kCommandMenu:
      // The page is a module document (abcui.gameui): it presents itself
      // from menu.state once the page is on top.
      break;
    case MenuPage::kInventory:
      // The page is a module document (abcui.gameui): it presents itself
      // from menu.state once the page is on top.
      break;
    case MenuPage::kLoading:
      // The page is a module document (abcui.gameui): it presents itself
      // from menu.state (the attempt kind rides the payload) and renders
      // the drained model on the paint beat.
      break;
    default:
      break; // every current page is module-owned
    }
  }
  // T17: the server page's query ownership. The T18 browser lives at
  // kServers; opening the page is the user's trigger and starts the
  // ACTIVE TAB's list query (Internet on first open; a live request of
  // the same open is not re-triggered), leaving the page releases every
  // handle (release implies cancel for a still-live query). Exiting the
  // game releases through onShutdown's model shutdown.
  const bool serverPageOpen =
      !state.pages.empty() && state.pages.top() == MenuPage::kServers;
  if (serverPageOpen && !ServerBrowser.Open) {
    // Fresh opening: start the user-triggered query for the active
    // tab with the current filter pairs.
    if (ServerBrowser.Query.listRequest() == 0 && !ServerBrowser.Query.closed())
      Server->beginQuery(ServerBrowser.Query.kind());
  } else if (ServerBrowser.Open && !serverPageOpen) {
    Server->releaseQueries();
    Server->logQueryChange();
  }
  ServerBrowser.Open = serverPageOpen;
  if (state.pages.empty() || state.pages.top() != MenuPage::kSettings) {
    // T13b: the mic test is a page-scoped device session - leaving the
    // settings page stops it (the original's PageHide guarantee, extended to
    // the controller-driven leave path). The page re-projects the state from
    // the published generation.
    VoiceModel.stopTest();
    services::settings::bumpPageRevision();
  }
  if (state.pages.empty() || state.pages.top() != MenuPage::kServers)
    Server->hide();

  // Overlay layer, top-most last. The loading page and the three
  // connection dialogs are module documents now (abcui.gameui): they
  // present from the state's overlay bitmask, and their document z-index
  // tiers (loading above the page shells and the console, the connection
  // dialogs above it) ride the descriptor (#4).
  // T16 replacement console: the page is a module document (abcui.gameui);
  // it presents from the state's consoleOpen fact and renders on the paint
  // beat. A close through its own surfaces (close button, routed ESC)
  // lowers the engine-facing answer through the section's close funnel, so
  // the sync only re-asserts it when the overlay dropped without one.
  // T14 overlays: the connection dialogs present from the bitmask; the
  // password dialog's re-prepare (a rejected retry clears the field) keys
  // on the model's per-generation prompt count.
  // The quit confirmation is a page module document now (abcui.gameui): it
  // presents itself from the overlay bit in menu.state.
  // T12: the display confirmation is a page module document now - it
  // presents itself from the video facts at the top-most tier, so a page
  // sync while it is up never raises another document over it.
  // T16 single truth: whenever the projection no longer holds the
  // console (routed ESC popped it, a force-dismiss cleared it), the
  // engine-facing answer must follow. hideConsole never notifies the sink,
  // so this cannot re-enter the sync; it also runs the engine-side
  // `hideconsole` the suppressed CGameUI::HideGameUI used to issue (issue
  // #5) - without it the engine's console flag outlived the close and the
  // next ActivateGameUI re-opened the console.
  if (!state.overlays.contains(MenuOverlay::kConsole) &&
      services::nativeUiBridge().consoleRequested()) {
    // The overlay dropped without the close funnel (a routed ESC): clear
    // the bridge truth AND the engine-side console flag (issue #5 - the
    // engine's console flag must never outlive the closed overlay).
    services::nativeUiBridge().noteConsoleDismissed();
    menuBackend().hideConsole();
  }
}

// The presentation sync every caller schedules: project the state machine
// onto the Rml documents, then publish the settled facts to the page modules
// (menu.state on a transition, menu.paint on every beat).
void GameUiModule::syncMenuPresentation() {
  projectMenuPresentation();
  publishMenuPresentation();
}

void GameUiModule::publishMenuPresentation() {
  services::menu::publish();
}

} // namespace gameui
