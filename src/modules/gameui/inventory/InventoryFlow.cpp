// gameui::InventoryFlow implementation - see InventoryFlow.h.
#include "InventoryFlow.h"

#include "../../../panels/inventory/InventoryPage.h"
#include "../../../services/GameUiBridge.h"
#include "rmlui/KeyMap.h"
#include "rmlui/VGUI2ExtensionImport.h"

#include <abc/Console.h>
#include <abc/CvarCmd.h>

#include <cstring>
#include <utility>

namespace gameui {

InventoryFlow::InventoryFlow(InventoryHost host) : Host(std::move(host)) {}

void InventoryFlow::setPage(panels::inventory::InventoryPage *page) {
  Page = page;
  if (Page)
    Page->setModel(&Model);
}

void InventoryFlow::shutdown() {
  Page = nullptr;
  Presented = false;
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
  // change unless the server clears it; only the presentation drops.
  hide();
  Requested = false;
  RaisedMenu = false;
}

void InventoryFlow::hide() {
  if (Page)
    Page->hide();
  Presented = false;
}

void InventoryFlow::open() {
  // The command can arrive before the first Rml context exists; replay the
  // intent once the context assembly finishes (same latch as CommandMenu).
  if (!Host.shellReady() || !Page) {
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
  if (Page->isVisible()) {
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

  Page->prepare();
  Host.dispatchOpen();
  Requested = false;
  Host.finishDispatch();
}

void InventoryFlow::close() {
  Requested = false;
  // -inventory also fires for windows this flow does not own (the native
  // handler hid whatever the viewport had active); only close our page.
  if (!Page || !Page->isVisible()) {
    RaisedMenu = false;
    return;
  }
  const bool raisedMenu = RaisedMenu;
  RaisedMenu = false;
  Page->hide();
  Presented = false;
  Host.closeNavigation(raisedMenu);
  Host.finishDispatch();
}

void InventoryFlow::present() {
  if (!Page)
    return;
  const bool wasVisible = Page->isVisible();
  Page->render();
  Page->show();
  if (!wasVisible)
    Page->refocusDefault();
  Presented = true;
}

bool InventoryFlow::handleKey(int keynum) {
  if (!Page || !Page->isVisible())
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

  const Rml::Input::KeyIdentifier key = rmlui::mapKeyCode(keynum);
  if (key == Rml::Input::KI_UNKNOWN)
    return false;
  return Page->handleKey(key);
}

void InventoryFlow::handleAction(services::inventory::Action action) {
  if (!Page || !Page->isVisible())
    return;
  const std::optional<std::string> command = Model.command(action);
  if (!command) {
    // No selection / empty list: the native handler issued nothing and kept
    // the window; re-project so a stale button state cannot linger.
    Page->render();
    return;
  }
  // Native order: the panel hides, then the client command runs. The command
  // is queued by pfnClientCmd, so issuing it before the close dispatch keeps
  // the same observable order.
  abc::EngineClientCmd(command->c_str());
  close();
}

} // namespace gameui
