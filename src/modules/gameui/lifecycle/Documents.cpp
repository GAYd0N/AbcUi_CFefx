// Extracted from GameUiModule.cpp (UTF-8). Responsibility: lifecycle/Documents.
#include <RmlUi/Core/Context.h>
#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/GameUiPages.h"
#include <abc/Console.h>
#include "framework/FatalExit.h"
#include "framework/Localization.h"
#include "services/console/ConsoleBackend.h"
#include "services/console/ConsoleBuffer.h"
#include <cstring>
#include <string>
#include <memory>
#include "modules/gameui/engine/MenuBindings.h"
namespace gameui {
using namespace engine;

void GameUiModule::onContextReady(Rml::Context *context) {
  Context = context;
  ServerBrowser.startContext();
  // One action seam for every window: fixed action names in, controller
  // intents out (never an original-UI command). T21: the emergency sink
  // joins it on the same seam - a degraded document's Quit button runs
  // the confirmed quit backend (a fault-time exit must not depend on
  // the quit-confirm overlay, which may be exactly what failed).
  const auto wire = [this, context](auto &page) {
    page->setActionSink([this](const char *action) { onMenuAction(action); });
    page->setEmergencySink([](const char *intent) {
      if (intent &&
          std::strcmp(intent,
                      framework::InteractiveDocument::kEmergencyQuitIntent) ==
              0)
        menuBackend().quitGame();
    });
    logLoadOutcome(page->load(context));
  };
  Shell = std::make_unique<panels::mainmenu::MainMenuPage>();
  Shell->setDefinition(UiConfig.menu());
  // Friends board join: the page reports the chosen row, the module owns the
  // validation and the typed connect intent (same path the browser uses).
  Shell->setFriendJoinSink([this](uint64_t steamId, const char *address) {
    onFriendJoin(steamId, address);
  });
  SubStub = std::make_unique<panels::menushared::MenuStubPage>(
      "menusub", "abc/abcui/gameui/rml/menustub.rml");
  // T16: the replacement console (was the menuconsole stub).
  Console = std::make_unique<panels::console::ConsolePage>();
  Console->setBuffer(&services::console::consoleBuffer());
  Console->setBackend(&services::console::consoleBackend());
  Console->setSubmitSink([](const std::string &command) {
    // The T15 submission path: one validated engine command per call
    // (pfnClientCmd inside the backend; the engine's own chain runs).
    using services::console::ConsoleBackend;
    const auto result =
        services::console::consoleBackend().submit(command.c_str());
    if (result != ConsoleBackend::SubmitResult::kExecutedUnknownVerb)
      return false;
    // The engine executes an unknown client command silently (Sven
    // Co-op's hw.dll carries no unknown-command message), and a loaded
    // level's server may own the verb and execute it - so the console's
    // own answer is exactly for the case where the client catalog is the
    // whole truth: outside a level. The page formats and shows the line;
    // engine call ownership stays here.
    return !services::nativeUiBridge().hasLevel();
  });
  Settings = std::make_unique<panels::settings::SettingsPage>();
  Settings->setPages(&UiConfig.pages());
  Settings->setModel(&SettingsModel);
  Settings->setBindModel(&BindModel);
  Settings->setVideoModel(&VideoModel);
  Settings->setVoiceModel(&VoiceModel);
  Settings->setFavorites(&ModelFavorites);
  SettingsView->setPage(Settings.get());
  Settings->setSpraySink([this](const std::string &path) { Spray->run(path); });
  // T18: the server browser page (was the kServers stub). The query is
  // the module's T17 member; the list model is the module-owned view;
  // the favorites store is Steam's authoritative one (T18b).
  Browser = std::make_unique<panels::serverbrowser::ServerBrowserPage>();
  Browser->setQuery(&ServerBrowser.Query);
  Browser->setListModel(&ServerBrowser.List);
  Browser->setBlacklist(&Blacklist);
  Browser->setFavorites(services::serverbrowser::steamFavorites());
  Server->setPage(Browser.get());
  // T19: the create-local-game page (was the kCreateGame stub).
  CreateGamePage_ = std::make_unique<panels::creategame::CreateGamePage>();
  Create->setPage(CreateGamePage_.get());
  // T20: the player-list page (was the kPlayerList stub).
  PlayerListPage_ = std::make_unique<panels::playerlist::PlayerListPage>();
  PlayerListPage_->setModel(&PlayerList);
  Player->setPage(PlayerListPage_.get());
  PlayerListPage_->setSelectionSink(
      [this](const services::playerlist::SelectionKey &Key) {
        Player->selectRow(Key);
      });
  VoteMenuPage_ = std::make_unique<panels::votemenu::VoteMenuPage>();
  Vote->setPage(VoteMenuPage_.get());
  CommandMenuPage_ = std::make_unique<panels::commandmenu::CommandMenuPage>();
  Command->setPage(CommandMenuPage_.get());
  CommandMenuPage_->setCommandSink(
      [this](const std::string &command) { Command->handleCommand(command); });
  CommandMenuPage_->setCloseSink([this] { Command->close(); });
  VideoConfirm = std::make_unique<panels::settings::VideoConfirmPage>();
  VideoConfirm->setVideoModel(&VideoModel);
  Video->setPage(VideoConfirm.get());
  ConfirmQuit = std::make_unique<panels::menushared::ConfirmQuitOverlay>();
  // T14 connection surfaces.
  Loading = std::make_unique<panels::loading::LoadingPage>();
  Loading->setModel(&LoadingModelState);
  ConnError = std::make_unique<panels::loading::ConnectionErrorOverlay>();
  Password = std::make_unique<panels::loading::PasswordOverlay>();
  DirectConnect = std::make_unique<panels::loading::DirectConnectOverlay>();
  wire(Shell);
  wire(SubStub);
  wire(Console);
  wire(Settings);
  wire(Browser);
  wire(CreateGamePage_);
  wire(PlayerListPage_);
  wire(VoteMenuPage_);
  wire(CommandMenuPage_);
  wire(VideoConfirm);
  wire(ConfirmQuit);
  wire(Loading);
  wire(ConnError);
  wire(Password);
  wire(DirectConnect);
  // T12 startup recovery: a persisted unconfirmed-switch record drives
  // the first presentation (confirmation window on the actual mode, or
  // an automatic write-back of the old values). Runs once per engine
  // session, before any menu may be raised.
  Video->resolveRecovery();
  // The takeover may have raised the menu before the context existed
  // (cold-start activation runs ~1.5s ahead of Rml assembly): the
  // pending show lands now, on the first context. A startup console
  // request (the -console/-toconsole latch) rides the same path: its
  // flag is sampled BEFORE the first presentation, because the phase
  // reconciliation inside presentMenu clears the overlay stack by
  // design (Booting -> Disconnected) and the sync's single-truth tail
  // then lowers the engine-facing answer. Re-requesting through the
  // bridge afterwards restores both truths and presents the overlay
  // (the sink pushes it once); the page's fresh-open projection
  // replays the output cache tail.
  const bool consolePending = services::nativeUiBridge().consoleRequested();
  if (services::nativeUiBridge().menuUp())
    presentMenu();
  if (consolePending)
    services::nativeUiBridge().requestConsoleOpen(
        services::NativeUiOrigin::kOwnUi);
  if (Vote->consumeRequest())
    Vote->open();
  if (Command->consumeRequest())
    Command->open();
}

void GameUiModule::logLoadOutcome(
    framework::InteractiveDocument::LoadOutcome outcome) {
  using LoadOutcome = framework::InteractiveDocument::LoadOutcome;
  switch (outcome) {
  case LoadOutcome::kLoaded:
    break;
  case LoadOutcome::kEmergencyFallback:
    abc::ConsoleWarn("[AbcUi] menu document degraded to the emergency page\n");
    break;
  case LoadOutcome::kFailed:
    // The inline memory document failed too: Rml cannot present
    // anything at all (fatal class), so the strict policy stops the
    // process with the external diagnosis instead of running blind.
    framework::fatalExit(
        "menu documents and the built-in emergency page both failed to load");
    break;
  }
}

void GameUiModule::serviceEmergencyPages() {
  if (!Context)
    return;
  framework::InteractiveDocument *pages[] = {
      Shell.get(),           SubStub.get(),
      Console.get(),         Settings.get(),
      Browser.get(),         CreateGamePage_.get(),
      PlayerListPage_.get(), VoteMenuPage_.get(),
      VideoConfirm.get(),    CommandMenuPage_.get(),
      ConfirmQuit.get(),     Loading.get(),
      ConnError.get(),       Password.get(),
      DirectConnect.get(),
  };
  bool recovered = false;
  for (framework::InteractiveDocument *page : pages) {
    if (!page || !page->retryRequested())
      continue;
    using LoadOutcome = framework::InteractiveDocument::LoadOutcome;
    const LoadOutcome outcome = page->retry(Context);
    if (outcome == LoadOutcome::kFailed) {
      // The main document failed AND the inline emergency document
      // failed: nothing this page can present - fatal class.
      framework::fatalExit("emergency retry: both the main document and the "
                           "emergency page failed");
      return;
    }
    abc::ConsolePrint(
        "[AbcUi] emergency retry \"%s\": %s\n", page->getName().c_str(),
        outcome == LoadOutcome::kLoaded ? "recovered" : "still degraded");
    if (outcome == LoadOutcome::kLoaded)
      recovered = true;
  }
  if (!recovered)
    return;
  // The recovered documents have empty caches by construction: clear
  // every "projected for this opening" latch and let the sync re-run
  // the fresh-open projections from the models.
  SettingsView->setPresented(false);
  ConsolePresented = false;
  ErrorPresented = false;
  PasswordPresented = false;
  DirectConnectPresented = false;
  LoadingVisible = false;
  Server->setPresented(false);
  Create->setPresented(false);
  Player->setPresented(false);
  Vote->setPresented(false);
  Command->setPresented(false);
  syncMenuPresentation();
}

} // namespace gameui
