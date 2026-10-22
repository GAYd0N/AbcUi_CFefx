// Debug-only console commands; extracted without changing command names.
// The console page is a module document now (abcui.gameui): these commands
// keep the bridge/status and catalog probes and drop the DOM readback.
#ifndef NDEBUG
#include <metahook.h>

#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/GameUiPages.h"
#include "DebugCommands.h"
#include <abc/Console.h>
#include "services/console/ConsoleBackend.h"
namespace gameui {
void DebugCommands::consoleUiStatusCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  auto &bridge = services::nativeUiBridge();
  abc::ConsolePrint("[AbcUi] console-ui: requested=%d visible=%d menu=%d\n",
                    bridge.consoleRequested() ? 1 : 0,
                    bridge.consoleVisible() ? 1 : 0,
                    self.MenuPresented ? 1 : 0);
  // Completion catalog probe (WS6): what the dropdown would show for "sv".
  const auto matches = services::console::consoleBackend().complete("sv", 12);
  abc::ConsolePrint("[AbcUi] console-ui: complete('sv') -> %u matches\n",
                    static_cast<unsigned>(matches.size()));
  for (std::size_t i = 0; i < matches.size() && i < 12; ++i)
    abc::ConsolePrint("[AbcUi] console-ui: match '%s'\n", matches[i].c_str());
}

void DebugCommands::consoleUiToggleCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  currentModule()->onMenuAction("console");
  consoleUiStatusCommand();
}

void DebugCommands::consoleUiSubmitCommand() {
  // The page's submission chain moved into the abcui.gameui module (the
  // editor and its key feed are module-owned); use the real console or the
  // engine's own command line.
  abc::ConsoleWarn("[AbcUi] console-ui submit: the editor is module-owned; "
                   "type into the console itself\n");
}

} // namespace gameui
#endif
