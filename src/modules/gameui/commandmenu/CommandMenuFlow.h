#pragma once

// gameui::CommandMenuFlow - the in-game command menu's open/close intents
// and claimed-key feed.
//
// The page became a module (abcui.gameui, sdk commandmenu section), so the
// flow keeps what is framework-side: the model, the definition hand-off,
// the request latch and the raised-menu discipline around open/close, and
// the key-edge feed (the +commandmenu close rule, then the
// menu.commandkey veto channel the page answers). Command execution stays
// here too: the services::commandmenu access point hands the page's
// activations to handleCommand (close first, then the engine send).

#include "../../../services/commandmenu/CommandMenuModel.h"

#include <functional>
#include <string>

namespace gameui {

struct CommandMenuHost {
  std::function<bool()> shellReady;
  // The command page is on top of the menu stack (the module document is
  // the presented surface).
  std::function<bool()> pageOnTop;
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

  void setDefinition(services::commandmenu::Definition definition);
  void shutdown();
  bool consumeRequest();
  void cancelPending();
  void resetForWorld();

  void toggle();
  void open();
  void close();
  void handleCommand(const std::string &command);
  bool handleKey(int keynum);

  services::commandmenu::Model &model() {
    return Model;
  }

private:
  CommandMenuHost Host;
  services::commandmenu::Model Model;
  bool Requested = false;
  bool RaisedMenu = false;
};

} // namespace gameui
