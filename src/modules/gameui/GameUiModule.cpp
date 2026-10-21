// Extracted from GameUiModule.cpp (UTF-8). Responsibility: GameUiModule.
#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/GameUiPages.h"
#include "../../panels/commandmenu/CommandMenuPage.h"
#include "../../panels/console/ConsolePage.h"
#include "../../panels/creategame/CreateGamePage.h"
#include "../../panels/inventory/InventoryPage.h"
#include "../../panels/loading/LoadingWindows.h"
#include "../../panels/mainmenu/MainMenuPage.h"
#include "../../panels/menushared/MenuShellWindows.h"
#include "../../panels/playerlist/PlayerListPage.h"
#include "../../panels/serverbrowser/ServerBrowserPage.h"
#include "../../panels/settings/SettingsPage.h"
#include "../../panels/settings/VideoConfirmPage.h"
#include "../../panels/votemenu/VoteMenuPage.h"

#include "../../data/WeaponResource.h"
#include "modules/gameui/settings/SettingsResources.h"
#include "modules/gameui/debug/DebugCommands.h"
#include <abc/Console.h>
#include <abc/Paths.h>
#include <abc/CvarCmd.h>
#include "services/serverbrowser/BlacklistFileStore.h"
#include "framework/Commands.h"
#include "data/NetworkMessages.h"
#include "modules/gameui/engine/MenuBindings.h"
#include "modules/gameui/engine/SettingsBindings.h"
#include "modules/gameui/engine/VideoBindings.h"
#include "modules/gameui/debug/ProbeCommand.h"
#include "rmlui/RmlUiModule.h"
#include <fstream>
#include <sstream>
namespace gameui {
using namespace engine;
using namespace settings;
namespace {

bool loadCommandMenuDefinition(services::commandmenu::Definition &definition,
                               std::string &error) {
  const std::string path =
      abc::ModPath("abc/abcui/gameui/config/commandmenu.json");
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    error = "cannot open file";
    return false;
  }
  std::ostringstream text;
  text << stream.rdbuf();
  std::string contents = text.str();
  if (contents.compare(0, 3, "\xef\xbb\xbf") == 0)
    contents.erase(0, 3);
  services::commandmenu::ParseError parseError;
  if (!services::commandmenu::DefinitionParser::parse(contents, definition,
                                                      parseError)) {
    error = parseError.Field + ": " + parseError.Message;
    return false;
  }
  return true;
}

} // namespace

GameUiModule::GameUiModule()
    : Controller(&menuBackend()), CaptureOwner(this),
      Video(std::make_unique<VideoFlow>(MakeVideoHost(), VideoModel)),
      Player(
          std::make_unique<PlayerListFlow>(MakePlayerListHost(), PlayerList)),
      Spray(std::make_unique<SprayMaker>([this](bool ok,
                                                const std::string &message,
                                                const std::string &previewUri) {
        if (Settings)
          Settings->setSprayResult(ok, message, previewUri);
      })),
      Vote(std::make_unique<VoteMenuFlow>(MakeVoteMenuHost(), voteMenuSeams())),
      Command(std::make_unique<CommandMenuFlow>(MakeCommandMenuHost())),
      Inventory(std::make_unique<InventoryFlow>(MakeInventoryHost())),
      Create(std::make_unique<CreateGameFlow>(MakeCreateGameHost(),
                                              createGameSeams())),
      Server(std::make_unique<ServerBrowserFlow>(MakeServerBrowserHost(),
                                                 ServerBrowser, Blacklist)),
      SettingsView(std::make_unique<SettingsFlow>(SettingsModel, BindModel,
                                                  VideoModel, VoiceModel)) {
  SettingsModel.setSeams(settingsSeams());
  BindModel.setSeams(bindSeams());
  VideoModel.setSeams(videoSeams());
  VoiceModel.setSeams(voiceSeams());
  // T19: the create-game launcher (typed cvar writes + the shipped
  // command sequence) and its backend relay. The thunk reaches the
  // module's request value object through the registry lookup, the
  // same shape the Debug diagnostics use.
  menuBackend().setLocalGameLauncher(&GameUiModule::launchLocalGame);
  // T20: the player-list model's roster/mute seams (playerres + the
  // voice service adapter, issue 17).
  PlayerList.setSeams(playerListSeams());
  // Vote creation menu: the live player roster comes from the same
  // playerres mirror; maps arrive through the MapList user message.
  // WS4: the friends board's Steam provider (bound lazily; without Steam the
  // board degrades to the local row plus an availability note).
  services::friends::friendsModel().setProvider(
      services::friends::steamFriendsProvider());
  modules::registry().add(this);
  services::nativeUiBridge().setMenuSink(this);
}

VideoHost GameUiModule::MakeVideoHost() {
  VideoHost host;
  host.context = [this] { return Context; };
  host.menuPresented = [this] { return MenuPresented; };
  host.syncMenuPresentation = [this] { syncMenuPresentation(); };
  host.hideMenuDocuments = [this] {
    if (Shell)
      Shell->hide();
    if (SubStub)
      SubStub->hide();
    if (Console)
      Console->hide();
    if (Settings)
      Settings->hide();
  };
  host.renderSettings = [this] {
    if (Settings)
      Settings->render();
  };
  return host;
}

PlayerListHost GameUiModule::MakePlayerListHost() {
  PlayerListHost host;
  host.dispatchBack = [this] {
    Controller.dispatch(MenuEvent{EventKind::kBack});
  };
  host.finishDispatch = [this] { finishDispatch(); };
  return host;
}

VoteMenuHost GameUiModule::MakeVoteMenuHost() {
  VoteMenuHost host;
  host.shellReady = [this] { return Shell && Context; };
  host.ensureMenu = [this] {
    if (!MenuPresented) {
      if (!services::nativeUiBridge().menuUp())
        services::nativeUiBridge().requestMenuUp(
            services::NativeUiOrigin::kOwnUi);
      if (!MenuPresented)
        presentMenu();
    }
    return MenuPresented;
  };
  host.reconcileInGame = [this] {
    reconcilePhase();
    return Controller.state().phase == menu::BackendPhase::kInGame;
  };
  host.pageOnTop = [this] {
    return Controller.state().pages.topIs(MenuPage::kVoteMenu);
  };
  host.dispatchOpen = [this] {
    Controller.dispatch(menu::makeOpenPage(MenuPage::kVoteMenu));
  };
  host.dispatchBack = [this] {
    Controller.dispatch(MenuEvent{EventKind::kBack});
  };
  host.finishDispatch = [this] { finishDispatch(); };
  return host;
}

CommandMenuHost GameUiModule::MakeCommandMenuHost() {
  CommandMenuHost host;
  host.shellReady = [this] { return Shell && Context; };
  host.ensureMenu = [this](bool &raisedMenu) {
    const bool menuWasUp = MenuPresented;
    if (!menuWasUp) {
      if (!services::nativeUiBridge().menuUp())
        services::nativeUiBridge().requestMenuUp(
            services::NativeUiOrigin::kOwnUi);
      if (!MenuPresented)
        presentMenu();
    }
    raisedMenu = !menuWasUp;
    return MenuPresented;
  };
  host.cancelRaisedMenu = [this] {
    services::nativeUiBridge().requestMenuDown(
        services::NativeUiOrigin::kOwnUi);
  };
  host.reconcileInGame = [this] {
    reconcilePhase();
    return Controller.state().phase == menu::BackendPhase::kInGame;
  };
  host.dispatchOpen = [this] {
    Controller.dispatch(menu::makeOpenPage(MenuPage::kCommandMenu));
  };
  host.closeNavigation = [this](bool raisedMenu) {
    if (!Controller.state().pages.topIs(MenuPage::kCommandMenu))
      return;
    Controller.dispatch(MenuEvent{EventKind::kBack}); // command page -> Pause
    if (!raisedMenu)
      return;
    if (Controller.state().pages.topIs(MenuPage::kPause))
      Controller.dispatch(MenuEvent{EventKind::kResume}); // close + resume
    // A phase change can make the decided resume illegal. The menu command
    // still owns this presentation, so force-clear the host instead of
    // leaving the pause page behind it.
    if (Controller.isMenuActive())
      Controller.dispatch(MenuEvent{EventKind::kMenuHidden});
  };
  host.finishDispatch = [this] { finishDispatch(); };
  return host;
}

InventoryHost GameUiModule::MakeInventoryHost() {
  InventoryHost host;
  host.shellReady = [this] { return Shell && Context; };
  host.ensureMenu = [this](bool &raisedMenu) {
    const bool menuWasUp = MenuPresented;
    if (!menuWasUp) {
      if (!services::nativeUiBridge().menuUp())
        services::nativeUiBridge().requestMenuUp(
            services::NativeUiOrigin::kOwnUi);
      if (!MenuPresented)
        presentMenu();
    }
    raisedMenu = !menuWasUp;
    return MenuPresented;
  };
  host.cancelRaisedMenu = [this] {
    services::nativeUiBridge().requestMenuDown(
        services::NativeUiOrigin::kOwnUi);
  };
  host.reconcileInGame = [this] {
    reconcilePhase();
    return Controller.state().phase == menu::BackendPhase::kInGame;
  };
  host.dispatchOpen = [this] {
    Controller.dispatch(menu::makeOpenPage(MenuPage::kInventory));
  };
  host.closeNavigation = [this](bool raisedMenu) {
    if (!Controller.state().pages.topIs(MenuPage::kInventory))
      return;
    Controller.dispatch(MenuEvent{EventKind::kBack}); // inventory -> Pause
    if (!raisedMenu)
      return;
    if (Controller.state().pages.topIs(MenuPage::kPause))
      Controller.dispatch(MenuEvent{EventKind::kResume}); // close + resume
    // Same recovery as the command menu: a phase change can make the decided
    // resume illegal, so force-clear the host instead of leaving the pause
    // page behind the closed window.
    if (Controller.isMenuActive())
      Controller.dispatch(MenuEvent{EventKind::kMenuHidden});
  };
  host.finishDispatch = [this] { finishDispatch(); };
  return host;
}

CreateGameHost GameUiModule::MakeCreateGameHost() {
  CreateGameHost host;
  host.dispatchCreate = [this](const std::string &map) {
    Controller.dispatch(menu::makeCreateGameRequest(map.c_str()));
  };
  host.dispatchBack = [this] {
    Controller.dispatch(MenuEvent{EventKind::kBack});
  };
  host.finishDispatch = [this] { finishDispatch(); };
  host.resetAutoRetry = [this] { AutoRetry.reset(); };
  return host;
}

ServerBrowserHost GameUiModule::MakeServerBrowserHost() {
  ServerBrowserHost host;
  host.dispatchConnect = [this](const std::string &address) {
    Controller.dispatch(menu::makeConnectRequest(address.c_str()));
  };
  host.finishDispatch = [this] { finishDispatch(); };
  host.resetAutoRetry = [this] { AutoRetry.reset(); };
  return host;
}

GameUiModule::~GameUiModule() = default;

const char *GameUiModule::name() const {
  return "gameui";
}

GameUiModule *currentModule() {
  return static_cast<GameUiModule *>(modules::registry().find("gameui"));
}

void GameUiModule::onInit() {
  rmlui::registerProbeCommand();
  // The RmlUi layer owns the context but does not include the module
  // registry; the composition root supplies the lifecycle dispatch.
  rmlui::SetLifecycleSink({
      [](Rml::Context *context) {
        modules::registry().contextReadyAll(context);
      },
      [] { modules::registry().shutdownAll(); },
      [](double frameTime) { modules::registry().frameAll(frameTime); },
      [] { modules::registry().vidInitAll(); },
  });
  // T17: the server data provider. The Steam adapter resolves its
  // interface lazily (Steam may not be up yet at HUD_Init) and is a
  // process-lifetime singleton; the model owns the async lifecycle.
  // hud_fastswitch: the data registry only reports the selection; the
  // engine command is an engine-facing concern owned here.
  weaponres::FastSwitchWeapon.append([](const weaponres::Weapon *weapon) {
    if (weapon)
      gEngfuncs.pfnServerCmd(weapon->name.c_str());
  });
  ServerBrowser.setProvider(services::serverbrowser::steamQueryProvider());
  const auto BlacklistPath = abc::ModPath("abc/abcui/server_blacklist.json");
  std::string BlacklistError;
  const bool BlacklistLoaded =
      services::serverbrowser::BlacklistFileStore::load(
          BlacklistPath, Blacklist, BlacklistError);
  if (!BlacklistLoaded)
    abc::ConsoleWarn("[AbcUi] %s: %s\n", BlacklistPath.c_str(),
                     BlacklistError.c_str());
  Blacklist.setSaveSink([BlacklistPath, BlacklistLoaded](const auto &Rules) {
    if (!BlacklistLoaded)
      return false; // Never overwrite a damaged user's file.
    std::string Error;
    const bool Saved = services::serverbrowser::BlacklistFileStore::save(
        BlacklistPath, Rules, Error);
    if (!Saved)
      abc::ConsoleWarn("[AbcUi] blacklist save: %s\n", Error.c_str());
    return Saved;
  });
  // New rules are stamped with the wall clock at the moment they are
  // blocked (the blacklist view shows it); the pure service never reads
  // the system time on its own.
  Blacklist.setClock(&services::serverbrowser::ServerBlacklist::nowSeconds);
  ServerBrowser.setBlacklist(&Blacklist);
  // I16 model favorites (issue 16): the player-model picker's persisted
  // star list. Missing file is the first run; an unreadable one keeps the
  // picker working read-only (the store refuses to overwrite it).
  switch (ModelFavorites.load()) {
  case services::settings::ModelFavorites::LoadStatus::kUnreadable:
    abc::ConsoleWarn("[AbcUi] model favorites store damaged; favorites are "
                     "read-only this session\n");
    break;
  case services::settings::ModelFavorites::LoadStatus::kMissing:
  case services::settings::ModelFavorites::LoadStatus::kLoaded:
    break;
  }
  // Startup catalog owns the strings backing the model's row views.
  // A live user.scr overrides the JSON fallback on its declared page.
  // Parsing/loading finishes before any document or transaction observes it.
  abc::CREATE_CVAR("abc_ui_browser_hide_unresponsive", "1", abc::FCVAR_VALUE,
                   nullptr);
  // Menu score (WS4): the shipped track plus its level; the music service
  // reads the source once per presentation change and the volume live.
  abc::CREATE_CVAR("abc_ui_menu_music",
                   "abc/abcui/gameui/scence/audio/opfor.mp3", abc::FCVAR_VALUE,
                   nullptr);
  // The character card's model source override; empty follows the player's
  // own `model` cvar (gameui::engine::playerCardFacts).
  abc::CREATE_CVAR("abc_ui_menu_model_path", "", abc::FCVAR_VALUE, nullptr);
  std::string ServerScript = readModFile("settings.scr");
  if (ServerScript.empty())
    ServerScript = readModFile("abc/abcui/gameui/config/server_options.scr");
  Create->setOptions(services::creategame::parseServerOptions(ServerScript));
  if (Create->optionsEmpty()) {
    abc::ConsoleWarn("[AbcUi] settings.scr unavailable or malformed; using "
                     "shipped server option definitions\n");
    Create->setOptions(services::creategame::parseServerOptions(
        readModFile("abc/abcui/gameui/config/server_options.scr")));
  }
  const auto Advanced = loadAdvancedScript();
  UiConfig.load(abc::ModPath("abc/abcui/gameui/config"), Advanced.Rows);
  for (const auto &Error : UiConfig.errors())
    abc::ConsoleWarn("[AbcUi] %s: %s: %s\n", Error.File.c_str(),
                     Error.Field.c_str(), Error.Message.c_str());
  services::commandmenu::Definition CommandDefinition;
  std::string CommandDefinitionError;
  if (loadCommandMenuDefinition(CommandDefinition, CommandDefinitionError))
    Command->setDefinition(std::move(CommandDefinition));
  else
    abc::ConsoleWarn("[AbcUi] commandmenu.json: %s\n",
                     CommandDefinitionError.c_str());
  SettingsModel.setTable(UiConfig.rows().data(),
                         static_cast<int>(UiConfig.rows().size()));
  // The multiplayer enumerations: directories (models/player) and the
  // spray basenames (logos) - the same scans the shipped list fills
  // run; empty scans leave the rows readable but unchangeable (the
  // live engine value stays selectable).
  for (const auto &Row : UiConfig.rows()) {
    if (Row.Kind != services::settings::SettingKind::kStringEnum ||
        !Row.StateName)
      continue;
    const std::string Source = Row.StateName;
    if (Source == "models/player")
      SettingsModel.setStringOptions(Row.Cvar,
                                     scanModDirectories("models/player"));
    else if (Source == "logos")
      SettingsModel.setStringOptions(Row.Cvar, scanModFileBasenames("logos"));
    else if (Source == "scence/video") {
      // Menu background videos: the player drops files into the plugin
      // tree; the dropdown lists them by stem and the cvar stores the
      // path the backdrop decoder opens.
      static const char *const VideoExtensions[] = {".mp4", ".webm", ".ivf",
                                                    ".avi", ".mkv"};
      SettingsModel.setStringChoices(
          Row.Cvar,
          scanModMediaFiles("abc/abcui/gameui/scence/video", VideoExtensions,
                            static_cast<int>(std::size(VideoExtensions)),
                            "abc/abcui/gameui/scence/video/"));
    } else if (Source == "scence/audio") {
      // Menu music: the engine's mp3 player resolves the path relative
      // to the mod dir (the legacy plugin played media/*.mp3 the same
      // way), so the scanned folder is the whole story.
      static const char *const AudioExtensions[] = {".mp3"};
      SettingsModel.setStringChoices(
          Row.Cvar,
          scanModMediaFiles("abc/abcui/gameui/scence/audio", AudioExtensions,
                            static_cast<int>(std::size(AudioExtensions)),
                            "abc/abcui/gameui/scence/audio/"));
    }
  }
  // T11 bind sources live in the mod dir (engine-owned data, not abcui
  // assets): the action list, the factory defaults and the label table.
  // Missing files degrade to an empty action table - the settings page
  // renders the bind section as unavailable (fail-closed).
  if (!BindModel.loadActions(readModFile("gfx/shell/kb_act.lst").c_str()))
    abc::ConsoleWarn(
        "[AbcUi] kb_act.lst malformed - key bindings unavailable\n");
  BindModel.loadDefaults(readModFile("gfx/shell/kb_def.lst").c_str());
  BindModel.loadLabels(readModFile("resource/valve_english.txt").c_str());
  // T19: the map catalog. The shipped create-game page enumerates
  // IFileSystem::FindFirst("maps/*.bsp") over the MOUNTED search paths
  // (GameUI.dll FUN_10005630 with pathID NULL + "GAMEDOWNLOAD"); the
  // same call here so content mounted from any search path is seen, not
  // just one disk directory. Unreadable/missing -> empty catalog (the
  // page then refuses every request fail-closed with CG_ErrorUnavailable).
  Create->loadCatalog();
  // The old ABCEnchance hook swallowed the client-side "votemenu" command
  // and opened its own dialog. AbcUi now owns that command and page.
  if (!VoteMenuCommandBound) {
    if (abc::CChainedCommand *command = commands::get("votemenu")) {
      command->Event().append([this]() {
        Vote->open();
        return false; // always keep the legacy GameUI dialog closed
      });
      VoteMenuCommandBound = true;
    } else {
      abc::ConsoleWarn("[AbcUi] votemenu command unavailable; pause entry "
                       "still opens the vote page\n");
    }
  }
  if (!VoteMenuMapListBound) {
    netmsg::MapList.append([this](const netmsg::MapListData &data) {
      Vote->applyMapList(data.clearAll, data.allCount, data.start, data.end,
                         data.maps);
    });
    VoteMenuMapListBound = true;
  }
  if (!CommandMenuCommandBound) {
    abc::CChainedCommand *open = commands::get("+commandmenu");
    abc::CChainedCommand *release = commands::get("-commandmenu");
    if (open && release) {
      open->Event().append([this]() {
        Command->toggle();
        return false; // the native command menu never opens
      });
      release->Event().append([]() {
        return false; // +commandmenu owns the whole toggle session
      });
      CommandMenuCommandBound = true;
    } else {
      abc::ConsoleWarn("[AbcUi] commandmenu commands unavailable; the Rml "
                       "command menu cannot open\n");
    }
  }
  // Issue 10: the native inventory window. The three client commands open
  // the viewport panel (CInventoryPanel::ShowPanel); swallowing them here is
  // what keeps that panel off-screen. `inventory` and `+inventory` both open
  // (the native command did not toggle), `-inventory` closes.
  if (!InventoryCommandBound) {
    abc::CChainedCommand *open = commands::get("inventory");
    abc::CChainedCommand *hold = commands::get("+inventory");
    abc::CChainedCommand *release = commands::get("-inventory");
    if (open && hold && release) {
      open->Event().append([this]() {
        Inventory->open();
        return false; // the native CInventoryPanel never opens
      });
      hold->Event().append([this]() {
        Inventory->open();
        return false;
      });
      release->Event().append([this]() {
        Inventory->close();
        return false;
      });
      InventoryCommandBound = true;
    } else {
      abc::ConsoleWarn("[AbcUi] inventory commands unavailable; the Rml "
                       "inventory window cannot open\n");
    }
  }
  if (!InventoryFeedBound) {
    // The item stream feeds the model directly (the page owns only the DOM);
    // InvAdd/InvRemove are the same messages the native panel consumes, so
    // the list is read-through and complete even while the window is closed.
    netmsg::InvAdd.append([this](const netmsg::InvItemData &data) {
      Inventory->model().add(data);
    });
    netmsg::InvRemove.append(
        [this](int id, int force) { Inventory->model().remove(id, force); });
    InventoryFeedBound = true;
  }
#ifndef NDEBUG
  DebugCommands::registerCommands();
#endif
}

void GameUiModule::onVidInit() {
  // A map transition invalidates the map catalog. The server sends a fresh
  // MapList for the next session; the page is not kept over a world change.
  Vote->resetForWorld();
  Command->resetForWorld();
  Inventory->resetForWorld();
}

bool GameUiModule::launchLocalGame(const char *map, const char **reasonToken) {
  if (GameUiModule *module = currentModule())
    return module->Create->launchLocal(map, reasonToken);
  return false;
}

bool GameUiModule::consumeLaunchFailure() {
  if (!PendingLaunchFailure)
    return false;
  const char *reason = PendingLaunchFailure;
  PendingLaunchFailure = nullptr;
  const uint32_t live = LoadingModelState.generation();
  if (live != 0) {
    Controller.dispatch(
        menu::makeLoadingFinished(live, menu::LoadingOutcome::kFailed, reason));
    return true;
  }
  return false;
}

void GameUiModule::onShutdown() {
  rmlui::closeProbeDocument();
  // Context still alive here: every document closes and drops its
  // listeners before the DOM dies (module contract). The tokens
  // release first - a stale no-op when the router already reset.
  // The mic test is a device resource: stop it before anything tears
  // down (the pairing guarantee covers the shutdown path too).
  VoiceModel.stopTest();
  Spray->shutdown();
  CaptureToken.release();
  PageToken.release();
  // Stop callbacks and release queries before any observing document dies.
  ServerBrowser.closeContext();
  Shell.reset();
  SubStub.reset();
  Console.reset();
  SettingsView->shutdown();
  Settings.reset();
  Server->shutdown();
  Browser.reset();
  Create->shutdown();
  CreateGamePage_.reset();
  Player->shutdown();
  PlayerListPage_.reset();
  Vote->shutdown();
  VoteMenuPage_.reset();
  Command->shutdown();
  CommandMenuPage_.reset();
  Inventory->shutdown();
  InventoryPage_.reset();
  Video->shutdown();
  VideoConfirm.reset();
  ConfirmQuit.reset();
  Loading.reset();
  ConnError.reset();
  Password.reset();
  DirectConnect.reset();
  Context = nullptr;
  MenuPresented = false;
  ConsolePresented = false;
  LoadingVisible = false;
  ErrorPresented = false;
  PasswordPresented = false;
  PasswordReprepare = false;
  DirectConnectPresented = false;
  LastStubPage = MenuPage::kMain;
}

namespace {
GameUiModule RegisterModule;
}

} // namespace gameui
