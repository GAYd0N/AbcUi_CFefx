// Extracted from GameUiModule.cpp (UTF-8). Responsibility: lifecycle/Documents.
#include <RmlUi/Core/Context.h>
#include "modules/gameui/GameUiModule.h"
#include <abc/Console.h>
#include "services/console/ConsoleBackend.h"
#include "services/settings/SettingsPageService.h"
#include "services/console/ConsoleBuffer.h"
#include <string>
#include "modules/gameui/engine/MenuBindings.h"
namespace gameui {
using namespace engine;

void GameUiModule::onContextReady(Rml::Context *context) {
  Context = context;
  ServerBrowser.startContext();
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

} // namespace gameui
