// gameui::CommandMenuFlow implementation - see CommandMenuFlow.h.
#include "CommandMenuFlow.h"

#include "../../../panels/commandmenu/CommandMenuPage.h"
#include "../../../services/GameUiBridge.h"
#include "../engine/MenuBindings.h"
#include "rmlui/KeyMap.h"
#include "rmlui/VGUI2ExtensionImport.h"

#include <abc/Console.h>
#include <abc/CvarCmd.h>

#include <cstring>
#include <utility>

namespace gameui {

CommandMenuFlow::CommandMenuFlow(CommandMenuHost host)
    : Host(std::move(host)) {}

void CommandMenuFlow::setPage(panels::commandmenu::CommandMenuPage *page) {
  Page = page;
  if (Page)
    Page->setModel(&Model);
}

void CommandMenuFlow::setDefinition(
    services::commandmenu::Definition definition) {
  Model.setDefinition(std::move(definition));
}

void CommandMenuFlow::shutdown() {
  Page = nullptr;
  Presented = false;
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

void CommandMenuFlow::hide() {
  if (Page)
    Page->hide();
  Presented = false;
  RaisedMenu = false;
}

void CommandMenuFlow::resetForWorld() {
  Requested = false;
  RaisedMenu = false;
  hide();
}

void CommandMenuFlow::toggle() {
  // A second press that arrives before the Rml context exists cancels the
  // pending open instead of replaying a stale intent later.
  if (Requested) {
    Requested = false;
    return;
  }
  if (Page && Page->isVisible()) {
    close();
    return;
  }
  open();
}

void CommandMenuFlow::open() {
  if (!Host.shellReady() || !Page) {
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
  if (Page->isDegraded()) {
    abc::ConsoleWarn(
        "[AbcUi] commandmenu page is degraded; command menu not opened\n");
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

  Page->prepare(engine::currentMapName());
  Host.dispatchOpen();
  Requested = false;
  Host.finishDispatch();
}

void CommandMenuFlow::present() {
  if (!Page)
    return;
  const bool wasVisible = Page->isVisible();
  Page->render();
  Page->show();
  if (!wasVisible)
    Page->refocusDefault();
  Presented = true;
}

void CommandMenuFlow::close() {
  Requested = false;
  const bool raisedMenu = RaisedMenu;
  RaisedMenu = false;
  if (Page) {
    Page->hide();
    Presented = false;
  }
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
  if (!Page || !Page->isVisible())
    return false;

  // While the Rml page owns input the engine never dispatches the +command
  // binding; close on the same physical key by reading the live binding.
  const char *binding = vgui2ext::keyBindingForKey(keynum);
  if (binding && _stricmp(binding, "+commandmenu") == 0) {
    close();
    return true;
  }

  const Rml::Input::KeyIdentifier key = rmlui::mapKeyCode(keynum);
  if (key == Rml::Input::KI_UNKNOWN)
    return false;
  return Page->handleKey(key);
}

} // namespace gameui
