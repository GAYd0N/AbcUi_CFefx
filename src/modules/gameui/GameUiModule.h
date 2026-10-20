#pragma once
#include "navigation/MenuAction.h"
#include "../../framework/InteractiveDocument.h"
#include "../../framework/UiInputRouter.h"
// GameUI composition root and cross-domain navigation coordinator. Platform
// binding, resource loading, diagnostics and context query ownership are
// separate collaborators. Domain flow implementations live beside their
// peers; this header is internal to the gameui module, never a spine API.
#include "../Module.h"
#include "services/uiconfig/ConfigRepository.h"
#include "../../services/EngineUiBackend.h"
#include "../../services/GameUiBridge.h"
#include "../../services/loading/LoadingModel.h"
#include "../../services/loading/AutoRetryGate.h"
#include "../../services/playerlist/PlayerListModel.h"
#include "../../services/friends/FriendsModel.h"
#include "../../services/serverbrowser/ServerFavorites.h"
#include "../../services/serverbrowser/ServerListModel.h"
#include "../../services/serverbrowser/ServerQuery.h"
#include "../../services/settings/KeyBindModel.h"
#include "../../services/settings/ModelFavorites.h"
#include "../../services/settings/SettingsModel.h"
#include "../../services/settings/VideoModeModel.h"
#include "../../services/settings/VoiceModel.h"
#include "navigation/MenuController.h"
#include "../../services/serverbrowser/ServerBrowserSession.h"
#include "settings/SettingsResources.h"
#include "commandmenu/CommandMenuFlow.h"
#include "creategame/CreateGameFlow.h"
#include "playerlist/PlayerListFlow.h"
#include "settings/SprayMaker.h"
#include "serverbrowser/ServerBrowserFlow.h"
#include "settings/SettingsFlow.h"
#include "settings/VideoFlow.h"
#include "votemenu/VoteMenuFlow.h"
#include <memory>
#include <vector>
#include <string>
namespace panels {
namespace commandmenu {
class CommandMenuPage;
}
namespace console {
class ConsolePage;
}
namespace creategame {
class CreateGamePage;
}
namespace loading {
class LoadingPage;
class ConnectionErrorOverlay;
class PasswordOverlay;
class DirectConnectOverlay;
} // namespace loading
namespace mainmenu {
class MainMenuPage;
}
namespace menushared {
class MenuStubPage;
class ConfirmQuitOverlay;
} // namespace menushared
namespace playerlist {
class PlayerListPage;
}
namespace serverbrowser {
class ServerBrowserPage;
}
namespace settings {
class SettingsPage;
class VideoConfirmPage;
} // namespace settings
namespace votemenu {
class VoteMenuPage;
}
} // namespace panels

namespace gameui {
using menu::EventKind;
using menu::MenuEvent;
using menu::MenuOverlay;
using menu::MenuPage;
class DebugCommands;
class GameUiModule final : public modules::IUiModule,
                           private framework::uiinput::Owner,
                           private services::INativeMenuSink {
public:
  GameUiModule();
  ~GameUiModule() override;
  const char *name() const override;
  void onInit() override;
  void onFrame(double frameTime) override;
  void onContextReady(Rml::Context *context) override;
  void onVidInit() override;
  void onShutdown() override;

private:
  friend class DebugCommands;
  static void
  logLoadOutcome(framework::InteractiveDocument::LoadOutcome outcome);
  void startKeyCapture();
  void endKeyCapture();
  void onCapturedKeyEdge(int keynum);
  void onKeyCaptureForceLost();
  void serviceEmergencyPages();
  void onNativeMenuShow() override;
  void onNativeMenuHide() override;
  void onNativeConsoleOpen() override;
  void onNativeConsoleClose() override;
  void presentMenu();
  void dismissMenu();
  void reconcilePhase();
  void syncMenuPresentation();
  void showStubPage(panels::menushared::MenuStubPage *stub, MenuPage page,
                    bool console);
  void onMenuAction(const char *action);
  bool retryServerAttempt();
  void tickAutoRetry();
  void handleConnectionAction(navigation::ActionKind Kind);
  void onFriendJoin(uint64_t steamId, const char *address);

  void finishDispatch();
  bool consumeLaunchFailure();
  static bool launchLocalGame(const char *map, const char **reasonToken);
  void syncAttemptState();
  void drainLoadingFeed();
  void hideAllMenuDocuments();
  void focusLog(const char *phase);
  void pumpBackgroundWork();
  void pumpVisibleWork();
  bool claimsKey(int down, int keynum) override;
  bool claimsWheel() override;
  void onClaimedKeyDown(int keynum) override;
  void onOwnershipLost() override;
  class KeyCaptureOwner final : public framework::uiinput::Owner {
  public:
    explicit KeyCaptureOwner(GameUiModule *owner) : Owner(owner) {}
    bool claimsKey(int down, int keynum) override {
      (void)down;
      (void)keynum;
      return true;
    }
    bool claimsWheel() override {
      return true;
    }
    void onClaimedKeyDown(int keynum) override {
      Owner->onCapturedKeyEdge(keynum);
    }
    bool onClaimedWheelPulse(int keynum) override {
      Owner->onCapturedKeyEdge(keynum);
      return true; // the pulse is consumed - no page scroll
    }
    void onOwnershipLost() override {
      Owner->onKeyCaptureForceLost();
    }

  private:
    GameUiModule *Owner;
  };
  VideoHost MakeVideoHost();
  PlayerListHost MakePlayerListHost();
  VoteMenuHost MakeVoteMenuHost();
  CommandMenuHost MakeCommandMenuHost();
  CreateGameHost MakeCreateGameHost();
  ServerBrowserHost MakeServerBrowserHost();
  Rml::Context *Context = nullptr;
  gameui::menu::MenuController Controller;
  services::uiconfig::ConfigRepository UiConfig;
  services::settings::SettingsModel SettingsModel;
  services::settings::KeyBindModel BindModel;
  services::settings::ModelFavorites ModelFavorites;
  services::settings::VoiceModel VoiceModel;
  services::settings::VideoModeModel VideoModel;
  services::loading::LoadingModel LoadingModelState;
  services::serverbrowser::ServerBlacklist Blacklist;
  services::serverbrowser::ServerBrowserSession ServerBrowser;
  bool CurrentAttemptIsLocal = false;
  bool LastAttemptWasLocal = false;
  const char *PendingLaunchFailure = nullptr;
  services::playerlist::PlayerListModel PlayerList;
  uint32_t FriendsProjected = 0;
  // Debug diagnostics (abc_ui_menu_model): the last module frame delta, so a
  // stalled pump is distinguishable from a stalled refresh.
  double LastFrameTime = 0.0;
  bool VoteMenuCommandBound = false;
  bool VoteMenuMapListBound = false;
  bool CommandMenuCommandBound = false;
  // 自动重试 (the browser's toggle): a failed join attempt re-enters the
  // existing typed retry path, bounded per failure chain. The page owns the
  // toggle's truth; the gate owns the run policy.
  services::loading::AutoRetryGate AutoRetry;
  std::unique_ptr<panels::mainmenu::MainMenuPage> Shell;
  std::unique_ptr<panels::menushared::MenuStubPage> SubStub;
  std::unique_ptr<panels::console::ConsolePage> Console;
  std::unique_ptr<panels::settings::SettingsPage> Settings;
  std::unique_ptr<panels::serverbrowser::ServerBrowserPage> Browser;
  std::unique_ptr<panels::creategame::CreateGamePage>
      CreateGamePage_; // T19 page
  std::unique_ptr<panels::playerlist::PlayerListPage>
      PlayerListPage_; // T20 page
  std::unique_ptr<panels::votemenu::VoteMenuPage> VoteMenuPage_;
  std::unique_ptr<panels::commandmenu::CommandMenuPage> CommandMenuPage_;
  std::unique_ptr<panels::settings::VideoConfirmPage> VideoConfirm;
  std::unique_ptr<VideoFlow> Video;
  std::unique_ptr<PlayerListFlow> Player;
  std::unique_ptr<SprayMaker> Spray;
  std::unique_ptr<VoteMenuFlow> Vote;
  std::unique_ptr<CommandMenuFlow> Command;
  std::unique_ptr<CreateGameFlow> Create;
  std::unique_ptr<ServerBrowserFlow> Server;
  std::unique_ptr<SettingsFlow> SettingsView;
  std::unique_ptr<panels::menushared::ConfirmQuitOverlay> ConfirmQuit;
  std::unique_ptr<panels::loading::LoadingPage> Loading;
  std::unique_ptr<panels::loading::ConnectionErrorOverlay> ConnError;
  std::unique_ptr<panels::loading::PasswordOverlay> Password;
  std::unique_ptr<panels::loading::DirectConnectOverlay> DirectConnect;
  framework::uiinput::Token PageToken;
  KeyCaptureOwner CaptureOwner;
  framework::uiinput::Token CaptureToken;
  bool MenuPresented = false;
  bool ConsolePresented = false;
  bool LoadingVisible = false;
  bool ErrorPresented = false;
  bool PasswordPresented = false;
  bool DirectConnectPresented = false;
  bool PasswordInvalid = false;
  int PasswordPrompts = 0;
  bool PasswordReprepare = false;
  std::string LastErrorReason;
  uint32_t NextPhaseSession = 1;
  MenuPage LastStubPage = MenuPage::kMain;
};
// Debug commands and the backend thunk cannot capture a module pointer
// (AbcCore's ADD_COMMAND takes a bare function pointer), so they resolve the
// registered module by name instead of reading a public static.
GameUiModule *currentModule();

} // namespace gameui
