#pragma once

// gameui::CommandMenuFlow - the in-game command menu page, request latch and
// engine-command boundary.
//
// Owns the command-menu model and page pointer. The composition root supplies
// higher-level navigation operations (ensure menu, reconcile, close with the
// paused-menu recovery) so the flow does not know the MenuController.

#include "../../../services/commandmenu/CommandMenuModel.h"
#include "../navigation/MenuAction.h"

#include <functional>
#include <string>

namespace panels::commandmenu {
class CommandMenuPage;
}

namespace gameui {

struct CommandMenuHost {
  std::function<bool()> shellReady;
  // Raises the menu when needed and reports whether this call did so.
  std::function<bool(bool &raisedMenu)> ensureMenu;
  std::function<void()> cancelRaisedMenu;
  std::function<bool()> reconcileInGame;
  std::function<void()> dispatchOpen;
  // Closes the page's navigation state; `raisedMenu` applies the paused-menu
  // recovery exactly as the old GameUiModule::closeCommandMenuPage did.
  std::function<void(bool raisedMenu)> closeNavigation;
  std::function<void()> finishDispatch;
};

class CommandMenuFlow {
public:
  explicit CommandMenuFlow(CommandMenuHost host);

  void setPage(panels::commandmenu::CommandMenuPage *page);
  void setDefinition(services::commandmenu::Definition definition);
  void shutdown();
  bool consumeRequest();
  void setPresented(bool presented) {
    Presented = presented;
  }
  void cancelPending();
  void resetForWorld();
  void hide();

  void toggle();
  void open();
  void present();
  void close();
  void handleCommand(const std::string &command);
  bool handleKey(int keynum);

private:
  CommandMenuHost Host;
  services::commandmenu::Model Model;
  panels::commandmenu::CommandMenuPage *Page = nullptr;
  bool Presented = false;
  bool Requested = false;
  bool RaisedMenu = false;
};

} // namespace gameui
