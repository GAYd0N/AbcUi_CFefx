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
  const auto emergencySink = [](const char *intent) {
    if (intent &&
        std::strcmp(intent,
                    framework::InteractiveDocument::kEmergencyQuitIntent) == 0)
      menuBackend().quitGame();
  };
  const auto wire = [this, context, &emergencySink](auto &page) {
    page->setActionSink([this](const char *action) { onMenuAction(action); });
    page->setEmergencySink(emergencySink);
    logLoadOutcome(page->load(context));
  };
  // The menu home is a module document now (abcui.gameui): the definition
  // and the join intent reach it through the sdk menuconfig/social
  // sections.
  SubStub = std::make_unique<panels::menushared::MenuStubPage>(
      "menusub", "abc/abcui/gameui/rml/menustub.rml");
  // The console page is a module document now (abcui.gameui): the buffer,
  // the submission channel and the completion catalog reach it through the
  // sdk consolepage section.
  Settings = std::make_unique<panels::settings::SettingsPage>();
  Settings->setPages(&UiConfig.pages());
  Settings->setModel(&SettingsModel);
  Settings->setBindModel(&BindModel);
  Settings->setVideoModel(&VideoModel);
  Settings->setVoiceModel(&VoiceModel);
  Settings->setFavorites(&ModelFavorites);
  SettingsView->setPage(Settings.get());
  Settings->setSpraySink([this](const std::string &path) { Spray->run(path); });
  // The browser page is a module document now (abcui.gameui): the query
  // session reaches it through the sdk serverbrowser section, the Steam
  // stores and the blacklist stay here.
  // The create-game page and the inventory window are module documents
  // now (abcui.gameui): the catalog/options reach the page through the sdk
  // creategame section, the item stream feeds the model.
  VideoConfirm = std::make_unique<panels::settings::VideoConfirmPage>();
  VideoConfirm->setVideoModel(&VideoModel);
  Video->setPage(VideoConfirm.get());
  ConfirmQuit = std::make_unique<panels::menushared::ConfirmQuitOverlay>();
  // The loading page and the three connection dialogs are module documents
  // now (abcui.gameui): the drained attempt model and the controller's
  // typed edges reach them through the sdk connection section.
  wire(SubStub);
  wire(Settings);
  wire(VideoConfirm);
  wire(ConfirmQuit);
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
  if (Inventory->consumeRequest())
    Inventory->open();
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
      SubStub.get(),
      Settings.get(),
      VideoConfirm.get(),
      ConfirmQuit.get(),
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
  Server->setPresented(false);
  syncMenuPresentation();
}

} // namespace gameui
