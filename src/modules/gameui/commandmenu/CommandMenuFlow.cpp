// gameui::CommandMenuFlow implementation - see CommandMenuFlow.h.
#include "CommandMenuFlow.h"

#include <abcui/events.h>

#include "../../../modsys/EventBus.h"
#include "../../../services/GameUiBridge.h"
#include "../engine/MenuBindings.h"
#include "rmlui/VGUI2ExtensionImport.h"

#include <abc/Console.h>
#include <abc/CvarCmd.h>

#include <cstring>
#include <utility>

namespace gameui {

CommandMenuFlow::CommandMenuFlow(CommandMenuHost host)
    : Host(std::move(host)) {}

void CommandMenuFlow::setDefinition(
    services::commandmenu::Definition definition) {
  Model.setDefinition(std::move(definition));
}

void CommandMenuFlow::shutdown() {
  Requested = false;
  RaisedMenu = false;
}

bool CommandMenuFlow::consumeRequest() {
  const bool requested = Requested;
  Requested = false;
  return requested;
}

void CommandMenuFlow::cancelPending() {
  Requested = false;
  RaisedMenu = false;
}

void CommandMenuFlow::resetForWorld() {
  Requested = false;
  RaisedMenu = false;
}

void CommandMenuFlow::toggle() {
  // A second press that arrives before the Rml context exists cancels the
  // pending open instead of replaying a stale intent later.
  if (Requested) {
    Requested = false;
    return;
  }
  if (Host.pageOnTop()) {
    close();
    return;
  }
  open();
}

void CommandMenuFlow::open() {
  if (!Host.shellReady()) {
    // HUD_Init can observe the command before the first HUD_Frame builds the
    // Rml context. Replay the intent once context assembly finishes.
    Requested = true;
    return;
  }
  if (!services::nativeUiBridge().hasLevel()) {
    abc::ConsoleWarn(
        "[AbcUi] commandmenu is only available while connected to a server\n");
    return;
  }
  if (!Model.hasDefinition()) {
    abc::ConsoleWarn(
        "[AbcUi] commandmenu.json is unavailable; command menu not opened\n");
    return;
  }

  bool raisedMenu = false;
  if (!Host.ensureMenu(raisedMenu))
    return;
  RaisedMenu = raisedMenu;

  if (!Host.reconcileInGame()) {
    if (RaisedMenu)
      Host.cancelRaisedMenu();
    RaisedMenu = false;
    return;
  }

  // Fresh opening for the live map: reset to the root. The page module
  // projects the reset through menu.state (map filters re-resolve here).
  Model.open(engine::currentMapName());
  Host.dispatchOpen();
  Requested = false;
  Host.finishDispatch();
}

void CommandMenuFlow::close() {
  Requested = false;
  const bool raisedMenu = RaisedMenu;
  RaisedMenu = false;
  // The page module follows the stack pop through menu.state; the
  // raised-menu recovery (close + resume) stays framework-side.
  Host.closeNavigation(raisedMenu);
  Host.finishDispatch();
}

void CommandMenuFlow::handleCommand(const std::string &command) {
  if (command.empty())
    return;
  // Close first: the shared engine command path may synchronously raise a
  // replacement page (votemenu, console commands, media player, ...).
  close();
  std::string line = command;
  line.push_back('\n');
  abc::EngineClientCmd(line.c_str());
}

bool CommandMenuFlow::handleKey(int keynum) {
  if (!Host.pageOnTop())
    return false;

  // While the Rml page owns input the engine never dispatches the +command
  // binding; close on the same physical key by reading the live binding.
  const char *binding = vgui2ext::keyBindingForKey(keynum);
  if (binding && _stricmp(binding, "+commandmenu") == 0) {
    close();
    return true;
  }

  // The page interprets the edge (model navigation and its own dismiss);
  // a nonzero aggregate vote means consumed.
  AbcUiKeyEvent event{};
  event.down = 1;
  event.keynum = keynum;
  return framework::eventBus().publishVetoAggregate(
      ABCUI_CHANNEL_MENU_COMMAND_KEY, event);
}

} // namespace gameui
