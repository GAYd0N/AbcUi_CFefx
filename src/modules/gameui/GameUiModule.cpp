// Extracted from GameUiModule.cpp (UTF-8). Responsibility: GameUiModule.
#include "modules/gameui/GameUiModule.h"

#include <cstring>
#include <format>

#include "../../data/WeaponResource.h"
#include "../../framework/ResourceLimits.h"
#include "modules/gameui/settings/SettingsResources.h"
#include "modules/gameui/debug/DebugCommands.h"
#include <abc/Console.h>
#include <abc/Paths.h>
#include <abc/CvarCmd.h>
#include "services/commandmenu/CommandMenuService.h"
#include "services/creategame/CreateGameService.h"
#include "services/console/ConsolePageService.h"
#include "services/loading/ConnectionService.h"
#include "services/friends/FriendsService.h"
#include "services/inventory/InventoryService.h"
#include "services/serverbrowser/BlacklistFileStore.h"
#include "services/playerlist/PlayerListService.h"
#include "services/settings/SettingsPageService.h"
#include "services/uiconfig/MenuShellService.h"
#include "services/votemenu/VoteMenuService.h"
#include "framework/Commands.h"
#include "data/NetworkMessages.h"
#include "modsys/ModuleDocuments.h"
#include "modsys/ModuleVfs.h"
#include "modules/gameui/engine/MenuBindings.h"
#include "modules/gameui/engine/SettingsBindings.h"
#include "modules/gameui/engine/VideoBindings.h"
#include "modules/gameui/debug/ProbeCommand.h"
#include "rmlui/RmlUiModule.h"
#include <fstream>
#include <iterator>
#include <string_view>

namespace gameui {
using namespace engine;
using namespace settings;

// The menu section's page ids are ABI: pin the state-machine enum values to
// the SDK constants so a reordered enum fails to compile instead of
// misrouting a module page.
static_assert(static_cast<int>(menu::MenuPage::kMain) == ABCUI_MENU_PAGE_MAIN);
static_assert(static_cast<int>(menu::MenuPage::kPause) ==
              ABCUI_MENU_PAGE_PAUSE);
static_assert(static_cast<int>(menu::MenuPage::kSettings) ==
              ABCUI_MENU_PAGE_SETTINGS);
static_assert(static_cast<int>(menu::MenuPage::kServers) ==
              ABCUI_MENU_PAGE_SERVERS);
static_assert(static_cast<int>(menu::MenuPage::kCreateGame) ==
              ABCUI_MENU_PAGE_CREATE_GAME);
static_assert(static_cast<int>(menu::MenuPage::kPlayerList) ==
              ABCUI_MENU_PAGE_PLAYER_LIST);
static_assert(static_cast<int>(menu::MenuPage::kVoteMenu) ==
              ABCUI_MENU_PAGE_VOTE);
static_assert(static_cast<int>(menu::MenuPage::kCommandMenu) ==
              ABCUI_MENU_PAGE_COMMAND);
static_assert(static_cast<int>(menu::MenuPage::kInventory) ==
              ABCUI_MENU_PAGE_INVENTORY);
static_assert(static_cast<int>(menu::MenuPage::kLoading) ==
              ABCUI_MENU_PAGE_LOADING);
static_assert(static_cast<int>(menu::BackendPhase::kBooting) ==
              ABCUI_MENU_PHASE_BOOTING);
static_assert(static_cast<int>(menu::BackendPhase::kDisconnected) ==
              ABCUI_MENU_PHASE_DISCONNECTED);
static_assert(static_cast<int>(menu::BackendPhase::kConnecting) ==
              ABCUI_MENU_PHASE_CONNECTING);
static_assert(static_cast<int>(menu::BackendPhase::kLoading) ==
              ABCUI_MENU_PHASE_LOADING);
static_assert(static_cast<int>(menu::BackendPhase::kInGame) ==
              ABCUI_MENU_PHASE_IN_GAME);
static_assert(static_cast<int>(menu::BackendPhase::kShuttingDown) ==
              ABCUI_MENU_PHASE_SHUTTING_DOWN);
static_assert(static_cast<int>(menu::BackendPhase::kFatal) ==
              ABCUI_MENU_PHASE_FATAL);

namespace {

bool loadCommandMenuDefinition(services::commandmenu::Definition &definition,
                               std::string &error) {
  const std::string path =
      services::uiconfig::resourceRoot() + "/commandmenu.json";
  std::string contents;
  if (!framework::ModuleVfs::readText(path.c_str(), contents)) {
    error = "cannot open package entry";
    return false;
  }
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
      Spray(std::make_unique<SprayMaker>([](bool ok, const std::string &message,
                                            const std::string &previewUri) {
        services::settings::setSprayResult(ok, message, previewUri);
      })),
      Vote(std::make_unique<VoteMenuFlow>(MakeVoteMenuHost(), voteMenuSeams())),
      Command(std::make_unique<CommandMenuFlow>(MakeCommandMenuHost())),
      Inventory(std::make_unique<InventoryFlow>(MakeInventoryHost())),
      Create(std::make_unique<CreateGameFlow>(MakeCreateGameHost(),
                                              createGameSeams())),
      Server(std::make_unique<ServerBrowserFlow>(MakeServerBrowserHost(),
                                                 ServerBrowser, Blacklist)) {
  SettingsModel.setSeams(settingsSeams());
  BindModel.setSeams(bindSeams());
  VideoModel.setSeams(videoSeams());
  VoiceModel.setSeams(voiceSeams());
  // The settings page is a module now: it reads the four models, the page
  // tree and the favorite store through the sdk settings section, and acts
  // back through the two edges this composition root owns (the display-mode
  // transaction and the spray maker) plus the bind-capture session (the
  // router token lives here).
  services::settings::PageModels pageModels;
  pageModels.rows = &SettingsModel;
  pageModels.bind = &BindModel;
  pageModels.video = &VideoModel;
  pageModels.voice = &VoiceModel;
  pageModels.favorites = &ModelFavorites;
  pageModels.pages = &UiConfig.pages();
  services::settings::setPageModels(pageModels);
  services::settings::PageAccess pageAccess;
  pageAccess.videoApply = [this] { return Video->applyChange(); };
  pageAccess.videoKeep = [this] { return Video->confirmKeep(); };
  pageAccess.videoRevert = [this] { return Video->confirmRevert(); };
  pageAccess.sprayMake = [this](const std::string &path) {
    Spray->run(path);
    return true;
  };
  pageAccess.beginBindCapture = [this](int32_t actionIndex) {
    if (!BindModel.beginCapture(static_cast<int>(actionIndex)))
      return false;
    startKeyCapture();
    return BindModel.capturing();
  };
  services::settings::setPageAccess(pageAccess);
  // T19: the create-game launcher (typed cvar writes + the shipped
  // command sequence) and its backend relay. The thunk reaches the
  // module's request value object through the registry lookup, the
  // same shape the Debug diagnostics use.
  menuBackend().setLocalGameLauncher(&GameUiModule::launchLocalGame);
  // T20: the player-list model's roster/mute seams (playerres + the
  // voice service adapter, issue 17).
  PlayerList.setSeams(playerListSeams());
  // The player-list page module reads this model through the sdk
  // playerlist section.
  services::playerlist::setModel(&PlayerList);
  // The command-menu page module reads this model through the sdk
  // commandmenu section; its command/dismiss boundaries run here (close
  // first, then the engine send; the raised-menu close discipline). The
  // seams are bare function pointers, so they resolve the module through
  // the registry (the same shape the launcher thunk uses).
  services::commandmenu::Access commandAccess;
  commandAccess.model = &Command->model();
  commandAccess.submitCommand = [](const char *command) {
    if (GameUiModule *module = currentModule())
      module->Command->handleCommand(command ? command : "");
  };
  commandAccess.dismiss = [] {
    if (GameUiModule *module = currentModule())
      module->Command->close();
  };
  services::commandmenu::setAccess(commandAccess);
  // The menu-shell page module reads the definition and its engine facts
  // through the sdk menuconfig section (the composition root owns the
  // ConfigRepository and the engine reads).
  services::uiconfig::MenuShellAccess shellAccess;
  shellAccess.definition = &UiConfig.menu();
  shellAccess.multiplayer = [] {
    return engine::menuBackend().maxClientsFact() > 1;
  };
  shellAccess.playerCard = [](std::string &playerName, std::string &modelPath,
                              std::string &modelLabel, std::string &actionModel,
                              std::string &weaponModel, int &sequence) {
    const engine::PlayerCardFacts facts = engine::playerCardFacts();
    playerName = facts.PlayerName;
    modelPath = facts.ModelPath;
    modelLabel = facts.ModelName;
    actionModel = facts.ActionModel;
    weaponModel = facts.WeaponModel;
    sequence = facts.Sequence;
  };
  shellAccess.buildNote = [](std::string &note) {
    note = std::format("Sven Co-op build {}", abc::g_dwEngineBuildnum);
  };
  services::uiconfig::setShellAccess(shellAccess);
  // The shell's friends board join intent: validated here (the address
  // grammar is the direct-connect one) before it becomes an attempt.
  services::friends::JoinSeam joinSeam;
  joinSeam.join = [](uint64_t steamId, const char *address) {
    if (GameUiModule *module = currentModule())
      module->onFriendJoin(steamId, address);
  };
  services::friends::setJoinSeam(joinSeam);
  // The create-game page module reads the catalog/options through the sdk
  // creategame section and submits its form fields once on Apply (this
  // seam assembles, validates and dispatches them).
  services::creategame::Access createGameAccess;
  createGameAccess.model = &Create->model();
  createGameAccess.submit =
      [](const std::vector<std::pair<std::string, std::string>> &fields,
         std::string &hint) {
        if (GameUiModule *module = currentModule())
          return module->Create->apply(fields, hint);
        return false;
      };
  services::creategame::setAccess(createGameAccess);
  // The inventory page module reads this model through the sdk inventory
  // section; the actions' client commands run here (the window closes
  // first, then the queued command). The seams resolve the module through
  // the registry (the launcher-thunk shape).
  services::inventory::Access inventoryAccess;
  inventoryAccess.model = &Inventory->model();
  inventoryAccess.dismiss = [] {
    if (GameUiModule *module = currentModule())
      module->Inventory->close();
  };
  services::inventory::setAccess(inventoryAccess);
  // The server-browser SDK needs both its models and flow intents. Keep
  // this wiring process-scoped, like the other page services; context
  // recreation resets the session contents, not the session's address.
  services::serverbrowser::Access serverBrowserAccess;
  serverBrowserAccess.session = &ServerBrowser;
  serverBrowserAccess.blacklist = &Blacklist;
  serverBrowserAccess.setTab = [](int kind) {
    if (GameUiModule *module = currentModule())
      module->Server->beginQuery(
          static_cast<services::serverbrowser::QueryKind>(kind));
  };
  serverBrowserAccess.refresh = [] {
    if (GameUiModule *module = currentModule())
      module->Server->refreshOrStop();
  };
  serverBrowserAccess.stop = [] {
    if (GameUiModule *module = currentModule())
      module->Server->stop();
  };
  serverBrowserAccess.connect = [] {
    if (GameUiModule *module = currentModule())
      module->Server->connectSelected();
  };
  serverBrowserAccess.toggleFavorite = [] {
    if (GameUiModule *module = currentModule())
      module->Server->toggleSelectedFavorite();
  };
  serverBrowserAccess.applyFilters = [] {
    if (GameUiModule *module = currentModule())
      module->Server->applyFiltersQuery();
  };
  services::serverbrowser::setAccess(serverBrowserAccess);
  // The console page module's close button lowers both truths (the bridge
  // request path and the engine-side console flag) - the seams resolve the
  // module through the registry (the launcher-thunk shape).
  services::console::PageAccess consolePageAccess;
  consolePageAccess.requestClose = [] {
    if (GameUiModule *module = currentModule()) {
      services::nativeUiBridge().requestConsoleClose(
          services::NativeUiOrigin::kOwnUi);
      services::nativeUiBridge().noteConsoleDismissed();
      menuBackend().hideConsole();
    }
  };
  services::console::setPageAccess(consolePageAccess);
  // The connection pages module reads the drained attempt model and acts
  // through these typed edges (the controller layer's decisions stay in the
  // connectionXxx methods). The seams are bare function pointers: they
  // resolve the module through the registry (the launcher-thunk shape).
  services::loading::ConnectionAccess connectionAccess;
  connectionAccess.model = &LoadingModelState;
  connectionAccess.errorReason = [] {
    if (GameUiModule *module = currentModule())
      return module->Controller.state().errorReason;
    return std::string();
  };
  connectionAccess.cancelLoading = [] {
    if (GameUiModule *module = currentModule())
      return module->connectionCancelLoading();
    return false;
  };
  connectionAccess.retryFailed = [] {
    if (GameUiModule *module = currentModule())
      module->connectionRetryFailed();
  };
  connectionAccess.returnHome = [] {
    if (GameUiModule *module = currentModule())
      module->connectionReturnHome();
  };
  connectionAccess.submitPassword = [](const char *password) {
    if (GameUiModule *module = currentModule())
      return module->connectionSubmitPassword(password);
    return false;
  };
  connectionAccess.passwordCancel = [] {
    if (GameUiModule *module = currentModule())
      module->connectionPasswordCancel();
  };
  connectionAccess.connectAddress = [](const char *address,
                                       std::string &errorToken) {
    if (GameUiModule *module = currentModule())
      return module->connectionConnectAddress(address, errorToken);
    return false;
  };
  services::loading::setConnectionAccess(connectionAccess);
  // Vote creation menu: the live player roster comes from the same
  // playerres mirror; maps arrive through the MapList user message.
  // WS4: the friends board's Steam provider (bound lazily; without Steam the
  // board degrades to the local row plus an availability note).
  services::friends::friendsModel().setProvider(
      services::friends::steamFriendsProvider());
  modules::registry().add(this);
  services::nativeUiBridge().setMenuSink(this);
  // The vote page module reads this model through the sdk votemenu section.
  services::votemenu::setModel(&Vote->model());
  // Menu section backing: the page modules act through these four entry
  // points and read the settled state from menu.state/menu.paint. Bound once
  // (the module object is process-lifetime).
  services::menu::Host &menuHost = services::menu::host();
  menuHost.snapshot = [this] { return menuSnapshot(); };
  menuHost.openPage = [this](int32_t page) { return openMenuPage(page); };
  menuHost.back = [this] { menuBack(); };
  menuHost.ensureUp = [this] { return ensureMenuUp(); };
  menuHost.resume = [this] { return resumeFromMenu(); };
  menuHost.quit = [this] { menuQuit(); };
  menuHost.quitResolve = [this](int32_t confirmed) {
    menuQuitResolve(confirmed);
  };
  menuHost.disconnect = [this] { menuDisconnect(); };
  menuHost.toggleConsole = [this] { menuToggleConsole(); };
  menuHost.directConnect = [this] {
    handleConnectionAction(navigation::ActionKind::kDirectConnect);
    finishDispatch();
  };
  menuHost.quitNow = [this] { menuBackend().quitGame(); };
  menuHost.dispatchCommand = [this](const char *name) -> int32_t {
    if (!Context || !services::nativeUiBridge().armed())
      return 0;
    if (std::string_view(name) == "votemenu")
      Vote->open();
    else if (std::string_view(name) == "+commandmenu")
      Command->toggle();
    else if (std::string_view(name) == "-commandmenu") {
      // The press owns the toggle; release must not reopen the native menu.
    } else if (std::string_view(name) == "inventory" ||
               std::string_view(name) == "+inventory")
      Inventory->open();
    else if (std::string_view(name) == "-inventory")
      Inventory->close();
    else
      return 0;
    return 1;
  };
}

VideoHost GameUiModule::MakeVideoHost() {
  VideoHost host;
  host.context = [this] { return Context; };
  host.menuPresented = [this] { return MenuPresented; };
  host.syncMenuPresentation = [this] { syncMenuPresentation(); };
  host.hideMenuDocuments = [this] {
    // Every page is a module document now; the display confirmation outlives
    // a menu hide and stays up until it resolves.
    framework::module_documents::hideMenu();
  };
  return host;
}

VoteMenuHost GameUiModule::MakeVoteMenuHost() {
  VoteMenuHost host;
  host.shellReady = [this] { return Context != nullptr; };
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
  host.dispatchOpen = [this] {
    Controller.dispatch(menu::makeOpenPage(MenuPage::kVoteMenu));
  };
  host.finishDispatch = [this] { finishDispatch(); };
  return host;
}

CommandMenuHost GameUiModule::MakeCommandMenuHost() {
  CommandMenuHost host;
  host.shellReady = [this] { return Context != nullptr; };
  host.pageOnTop = [this] {
    return Controller.state().pages.topIs(MenuPage::kCommandMenu);
  };
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
  host.shellReady = [this] { return Context != nullptr; };
  host.pageOnTop = [this] {
    return Controller.state().pages.topIs(MenuPage::kInventory);
  };
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
    // Same server-data boundary as WeaponChoosePanel: only an HLSDK-shaped
    // weapon command token may reach the console.
    if (weapon && framework::ResourceLimits::weaponCommandName(weapon->name))
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
  // The character card's model source override; empty follows the player's
  // own `model` cvar (gameui::engine::playerCardFacts).
  abc::CREATE_CVAR("abc_ui_menu_model_path", "", abc::FCVAR_VALUE, nullptr);
  // The card's action group (the stance model that poses the displayed model
  // and the weapon), its sequence, and the weapon model merged onto it. A
  // bare action spelling lives in abc/abcui/models/menu_action/. A weapon
  // requires its complete game-relative .mdl path, used verbatim; empty
  // means no weapon.
  abc::CREATE_CVAR("abc_ui_menu_action_model", "display_male", abc::FCVAR_VALUE,
                   nullptr);
  abc::CREATE_CVAR("abc_ui_menu_model_sequence", "0", abc::FCVAR_VALUE,
                   nullptr);
  abc::CREATE_CVAR("abc_ui_menu_weapon_model", "models/p_m16.mdl",
                   abc::FCVAR_VALUE, nullptr);
  std::string ServerScript = readModFile("settings.scr");
  if (ServerScript.empty())
    framework::ModuleVfs::readText(
        (services::uiconfig::resourceRoot() + "/server_options.scr").c_str(),
        ServerScript);
  Create->setOptions(services::creategame::parseServerOptions(ServerScript));
  if (Create->optionsEmpty()) {
    abc::ConsoleWarn("[AbcUi] settings.scr unavailable or malformed; using "
                     "shipped server option definitions\n");
    framework::ModuleVfs::readText(
        (services::uiconfig::resourceRoot() + "/server_options.scr").c_str(),
        ServerScript);
    Create->setOptions(services::creategame::parseServerOptions(ServerScript));
  }
  const auto Advanced = loadAdvancedScript();
  const auto &root = services::uiconfig::resourceRoot();
  if (!root.empty())
    UiConfig.load(
        root,
        [](const std::string &uri, std::string &text) {
          return framework::ModuleVfs::readText(uri.c_str(), text);
        },
        [](const std::string &uri) {
          return framework::ModuleVfs::list(uri.c_str());
        },
        Advanced.Rows);
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
    else if (Source == "scence/video" || Source == "scence/audio") {
      std::string directory;
      const std::string suffix = Source == "scence/video" ? "video" : "audio";
      framework::ModuleVfs::resolve(root + "/_", "../scence/" + suffix + "/_",
                                    directory);
      if (directory.size() >= 2)
        directory.resize(directory.size() - 2);
      std::vector<services::settings::StringChoice> choices;
      for (const auto &uri : framework::ModuleVfs::list(directory.c_str())) {
        const auto slash = uri.rfind('/');
        const auto dot = uri.rfind('.');
        if (dot == uri.npos || dot < slash)
          continue;
        const auto extension = uri.substr(dot);
        const bool supported =
            suffix == "audio" ? (extension == ".mp3" || extension == ".wav" ||
                                 extension == ".ogg")
                              : (extension == ".mp4" || extension == ".webm" ||
                                 extension == ".mkv" || extension == ".avi" ||
                                 extension == ".ivf");
        if (supported && choices.size() < 512)
          choices.push_back({uri, uri.substr(slash + 1, dot - slash - 1)});
      }
      SettingsModel.setStringChoices(Row.Cvar, std::move(choices));
    }
  }
  configureMenuActionChoices(SettingsModel);

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
  if (!VoteMenuMapListBound) {
    netmsg::MapList.append([this](const netmsg::MapListData &data) {
      Vote->applyMapList(data.clearAll, data.allCount, data.start, data.end,
                         data.maps);
    });
    VoteMenuMapListBound = true;
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
  Server->shutdown();
  Create->shutdown();
  Vote->shutdown();
  Command->shutdown();
  Inventory->shutdown();
  Video->shutdown();
  Context = nullptr;
  MenuPresented = false;
}

namespace {
GameUiModule RegisterModule;
}

} // namespace gameui
