// gameui::InventoryFlow implementation - see InventoryFlow.h.
#include "InventoryFlow.h"

#include <abcui/events.h>

#include "../../../modsys/EventBus.h"
#include "../../../services/GameUiBridge.h"
#include "rmlui/VGUI2ExtensionImport.h"

#include <abc/Console.h>

#include <cstring>
#include <utility>

namespace gameui {

InventoryFlow::InventoryFlow(InventoryHost host) : Host(std::move(host)) {}

void InventoryFlow::shutdown() {
  Requested = false;
  RaisedMenu = false;
}

bool InventoryFlow::consumeRequest() {
  const bool requested = Requested;
  Requested = false;
  return requested;
}

void InventoryFlow::resetForWorld() {
  // The item list itself belongs to the server stream and survives a map
  // change unless the server clears it; only the presentation drops (the
  // module document follows the menu layer).
  Requested = false;
  RaisedMenu = false;
}

void InventoryFlow::open() {
  // The command can arrive before the first Rml context exists; replay the
  // intent once the context assembly finishes (same latch as CommandMenu).
  if (!Host.shellReady()) {
    Requested = true;
    return;
  }
  if (!services::nativeUiBridge().hasLevel()) {
    abc::ConsoleWarn(
        "[AbcUi] inventory is only available while connected to a server\n");
    return;
  }
  // The native command opened an already-open panel as a no-op
  // (CInventoryPanel::ShowPanel early-returns when it is the active panel).
  if (Host.pageOnTop()) {
    Requested = false;
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

  Host.dispatchOpen();
  Requested = false;
  Host.finishDispatch();
}

void InventoryFlow::close() {
  Requested = false;
  // -inventory also fires for windows this flow does not own (the native
  // handler hid whatever the viewport had active); only close our page.
  if (!Host.pageOnTop()) {
    RaisedMenu = false;
    return;
  }
  const bool raisedMenu = RaisedMenu;
  RaisedMenu = false;
  // The module document follows the stack pop through menu.state; the
  // raised-menu recovery (close + resume) stays framework-side.
  Host.closeNavigation(raisedMenu);
  Host.finishDispatch();
}

bool InventoryFlow::handleKey(int keynum) {
  if (!Host.pageOnTop())
    return false;

  // While the menu owns input the engine never dispatches the bound command;
  // close on the same physical key by reading the live binding. The native
  // hold semantics (-inventory on release) cannot be delivered through the
  // UI owner, so the bound key is a toggle while the window is up.
  const char *binding = vgui2ext::keyBindingForKey(keynum);
  if (binding && (_stricmp(binding, "+inventory") == 0 ||
                  _stricmp(binding, "inventory") == 0)) {
    close();
    return true;
  }

  // The page interprets the edge (list navigation and its own dismiss);
  // a nonzero aggregate vote means consumed.
  AbcUiKeyEvent event{};
  event.down = 1;
  event.keynum = keynum;
  return framework::eventBus().publishVetoAggregate(
      ABCUI_CHANNEL_MENU_COMMAND_KEY, event);
}

} // namespace gameui
