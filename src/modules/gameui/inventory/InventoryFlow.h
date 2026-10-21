#pragma once

// gameui::InventoryFlow - the in-game inventory window's open/close flow.
//
// Owns the inventory model and the page pointer. The composition root
// supplies the navigation callbacks (raise the menu, reconcile to the
// in-game phase, dispatch open/back, finish the dispatch), so the flow never
// knows the MenuController. The chained inventory/+inventory/-inventory
// commands are the only open/close entry points besides the page's own close
// button; the native CInventoryPanel handler is swallowed by returning false
// from every hook.
//
// Actions are read-through: the flow asks the model for a command (built
// from the current selection), sends it once through
// abc::EngineClientCmd, and closes exactly like the native window (it hid
// itself before issuing the command). No list mutation happens here - a
// successful drop only removes the row when the server's InvRemove arrives.

#include "../../../services/inventory/InventoryModel.h"

#include <functional>

namespace panels::inventory {
class InventoryPage;
}

namespace gameui {

struct InventoryHost {
  std::function<bool()> shellReady;
  // Raises the menu when needed and reports whether this call did so.
  std::function<bool(bool &raisedMenu)> ensureMenu;
  std::function<void()> cancelRaisedMenu;
  std::function<bool()> reconcileInGame;
  std::function<void()> dispatchOpen;
  // Closes the page's navigation state; `raisedMenu` applies the paused-menu
  // recovery exactly as CommandMenuFlow does.
  std::function<void(bool raisedMenu)> closeNavigation;
  std::function<void()> finishDispatch;
};

class InventoryFlow {
public:
  explicit InventoryFlow(InventoryHost host);

  void setPage(panels::inventory::InventoryPage *page);
  void shutdown();
  bool consumeRequest();
  void setPresented(bool presented) {
    Presented = presented;
  }
  void resetForWorld();
  void hide();

  void open();
  void close();
  void present();
  bool handleKey(int keynum);
  void handleAction(services::inventory::Action action);

  services::inventory::InventoryModel &model() {
    return Model;
  }

private:
  InventoryHost Host;
  services::inventory::InventoryModel Model;
  panels::inventory::InventoryPage *Page = nullptr;
  bool Presented = false;
  bool Requested = false;
  bool RaisedMenu = false;
};

} // namespace gameui
