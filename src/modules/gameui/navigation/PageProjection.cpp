// Extracted from GameUiModule.cpp (UTF-8). Responsibility:
// navigation/PageProjection.
#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/GameUiPages.h"
#include <abc/Plugin.h>
#include "modules/gameui/engine/MenuBindings.h"
namespace gameui {
using namespace engine;
void GameUiModule::syncMenuPresentation() {
  if (!Shell || !Context)
    return;
  const auto &state = Controller.state();
  if (!Controller.isMenuActive()) {
    hideAllMenuDocuments();
    if (ServerBrowser.Open || ServerBrowser.Query.listRequest() != 0)
      Server->releaseQueries();
    return;
  }
  // GameMenu.res conditions, verified against the shipped GameUI
  // (CGameMenu::UpdateMenu 0x1000dbe0): "notsingle" rows hide only
  // in-game+singleplayer - the no-map home always shows them, and the
  // in-game half of the condition IS the page split (Main vs Pause).
  // The player list row (OnlyInGame + notsingle) therefore keys on the
  // maxplayers fact alone on the pause page.
  const bool multiplayer = menuBackend().maxClientsFact() > 1;
  Shell->setMultiplayer(multiplayer);
  // Character card + footer facts: engine cvar values the player edits in
  // Settings > Multiplayer; the page only projects them.
  {
    const auto Facts = engine::playerCardFacts();
    Shell->setPlayerCard(Facts.PlayerName.c_str(), Facts.ModelPath.c_str(),
                         Facts.ModelName.c_str());
    char build[64];
    _snprintf_s(build, sizeof(build), _TRUNCATE, "Sven Co-op build %u",
                abc::g_dwEngineBuildnum);
    Shell->setFooterNote(build);
  }

  // Project each implemented page from navigation state. The stub is only
  // the defensive default for a page without a registered projection;
  // it never reopens a native window.
  bool shellVisible = false;
  bool loadingVisible = false;
  if (!state.pages.empty()) {
    const bool overlaysHoldFocus = !state.overlays.empty();
    switch (state.pages.top()) {
    case MenuPage::kMain:
    case MenuPage::kPause: {
      shellVisible = true;
      // Show before the section focus pass: Focus() needs the
      // document visible (first presentation has no frame between).
      // The per-frame focus guard (onFrame) re-takes the section
      // default against focus thieves.
      Shell->show();
      const auto section =
          state.pages.top() == MenuPage::kMain
              ? panels::mainmenu::MainMenuPage::Section::kMain
              : panels::mainmenu::MainMenuPage::Section::kPause;
      Shell->showSection(section, !overlaysHoldFocus);
      break;
    }
    case MenuPage::kSettings:
      SettingsView->present();
      break;
    case MenuPage::kServers:
      // T18: the real browser replaced the T09 stub. tick() runs on
      // the frame beat; the fresh-open query trigger is the T17
      // block below.
      Server->present();
      break;
    case MenuPage::kCreateGame:
      // T19: the real create-game page replaced the T09 stub.
      Create->present();
      break;
    case MenuPage::kPlayerList:
      // T20: the real player list replaced the T09 stub. Fresh
      // roster+mute read on every sync (read-through; the frame beat
      // re-reads while presented).
      Player->present();
      break;
    case MenuPage::kVoteMenu:
      Vote->present();
      break;
    case MenuPage::kCommandMenu:
      Command->present();
      break;
    case MenuPage::kLoading:
      // Fresh projection on every sync: the attempt's values moved
      // since the last frame (the frame drain re-renders too). T19:
      // a local-server start carries its own loading title; an engine
      // changelevel gets the map-load title/status instead of the connect
      // wording (there is no server address to contact).
      if (Loading) {
        const auto kind = state.attemptKind;
        const bool mapLoad = kind == menu::AttemptKind::kMapLoad;
        Loading->setTitleToken(
            kind == menu::AttemptKind::kLocalGame
                ? "#CG_LoadingTitle"
                : (mapLoad ? "#Loading_MapTitle" : ""));
        Loading->setStatusToken(mapLoad ? "#Loading_MapStatus" : "");
        Loading->render();
        Loading->show();
        loadingVisible = true;
        LoadingVisible = true;
      }
      break;
    default:
      showStubPage(SubStub.get(), state.pages.top(), false);
      break;
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
  if (shellVisible)
    SubStub->hide();
  else
    Shell->hide();
  if (!loadingVisible && Loading) {
    Loading->hide();
    LoadingVisible = false;
  }
  if (state.pages.empty() || state.pages.top() != MenuPage::kSettings) {
    SettingsView->hide();
    // T13b: the mic test is a page-scoped device session - leaving
    // the settings page stops it (the original's PageHide guarantee,
    // extended to the controller-driven leave path).
    VoiceModel.stopTest();
  }
  if (state.pages.empty() || state.pages.top() != MenuPage::kServers)
    Server->hide();
  if (state.pages.empty() || state.pages.top() != MenuPage::kCreateGame)
    Create->hide();
  if (state.pages.empty() || state.pages.top() != MenuPage::kPlayerList)
    Player->hide();
  if (state.pages.empty() || state.pages.top() != MenuPage::kVoteMenu)
    Vote->hide();
  if (state.pages.empty() || state.pages.top() != MenuPage::kCommandMenu)
    Command->hide();

  // Overlay layer, top-most last (each show pulls to the context
  // front); idempotent shows neither re-focus nor re-order. The loading
  // page (shown in the page switch above) is NOT ordered by this block:
  // its document z-index tier (InteractiveDocument::kLoadingZIndex) keeps
  // it above the page shells and the console even though the console
  // shows later, while the connection dialogs and the quit confirmation
  // carry higher tiers of their own (#4).
  // T16 replacement console: fresh projection on every sync (the
  // page appends only new lines; showLinesForTest analog = render).
  bool consoleDropped = false;
  if (state.overlays.contains(MenuOverlay::kConsole) && Console) {
    if (!ConsolePresented) {
      Console->prepare(); // fresh open: clear editor, reseed view
      ConsolePresented = true;
    }
    Console->render();
    Console->show();
  } else if (Console) {
    Console->hide();
    // Issue #5: the UI closed the console (routed ESC, the close button, a
    // force-dismiss). ConsolePresented is the only witness left at this
    // point - requestConsoleClose already lowered the bridge truth, so a
    // consoleRequested() test would miss exactly this path. The engine-side
    // `hideconsole` follows below.
    consoleDropped = ConsolePresented;
    ConsolePresented = false;
  }
  // T14 overlays. Each "presented" latch means "projected at least
  // once for this opening" - the prepare() clears the fields exactly
  // once per open, repeated syncs are idempotent shows.
  if (state.overlays.contains(MenuOverlay::kConnectionError) && ConnError) {
    if (!ErrorPresented || LastErrorReason != state.errorReason) {
      ConnError->showFor(state.errorReason.c_str());
      LastErrorReason = state.errorReason;
      ErrorPresented = true;
    }
    ConnError->show();
  } else if (ConnError) {
    ConnError->hide();
    ErrorPresented = false;
  }
  if (state.overlays.contains(MenuOverlay::kPassword) && Password) {
    if (!PasswordPresented || PasswordReprepare) {
      Password->prepare(PasswordInvalid);
      PasswordPresented = true;
      PasswordReprepare = false;
    }
    Password->show();
  } else if (Password) {
    Password->hide();
    PasswordPresented = false;
  }
  if (state.overlays.contains(MenuOverlay::kDirectConnect) && DirectConnect) {
    if (!DirectConnectPresented) {
      DirectConnect->prepare();
      DirectConnectPresented = true;
    }
    DirectConnect->show();
  } else if (DirectConnect) {
    DirectConnect->hide();
    DirectConnectPresented = false;
  }
  if (state.overlays.contains(MenuOverlay::kConfirmQuit))
    ConfirmQuit->show();
  else
    ConfirmQuit->hide();
  // T12: the confirmation page is the top-most layer of all - a page
  // sync while it is up must not raise another document over it.
  if (Video->confirmPresented() && VideoConfirm)
    VideoConfirm->show();
  // T16 single truth: whenever the projection no longer holds the
  // console (routed ESC popped it, a force-dismiss cleared it), the
  // engine-facing answer must follow. hideConsole never notifies the sink,
  // so this cannot re-enter the sync; it also runs the engine-side
  // `hideconsole` the suppressed CGameUI::HideGameUI used to issue (issue
  // #5) - without it the engine's console flag outlived the close and the
  // next ActivateGameUI re-opened the console.
  if (consoleDropped)
    menuBackend().hideConsole();
  else if (!state.overlays.contains(MenuOverlay::kConsole) &&
           services::nativeUiBridge().consoleRequested())
    services::nativeUiBridge().noteConsoleDismissed();
}

} // namespace gameui
