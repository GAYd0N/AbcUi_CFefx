#pragma once

// gameui::VoteMenuFlow - vote-creation model, request latch and submit path.
//
// The page became a module (abcui.gameui, sdk votemenu section), so the flow
// keeps only what is framework-side: the model, the server MapList feed and
// the open intent that raises the menu and pushes the page. The module
// presents the document from menu.state and sends the validated command
// itself (console->serverCommand + menu->back).

#include "../../../services/votemenu/VoteMenuModel.h"
#include "../navigation/MenuAction.h"

#include <functional>
#include <string>
#include <vector>

namespace gameui {

struct VoteMenuHost {
  std::function<bool()> shellReady;
  std::function<bool()> ensureMenu;
  std::function<bool()> reconcileInGame;
  std::function<void()> dispatchOpen;
  std::function<void()> finishDispatch;
};

class VoteMenuFlow {
public:
  VoteMenuFlow(VoteMenuHost host,
               services::votemenu::VoteMenuModel::Seams seams);

  void shutdown();
  bool consumeRequest();
  void resetForWorld();

  void open();
  void applyMapList(int clearAll, int allCount, int start, int end,
                    const std::vector<std::string> &maps);

  services::votemenu::VoteMenuModel &model() {
    return Model;
  }

private:
  VoteMenuHost Host;
  services::votemenu::VoteMenuModel Model;
  bool Requested = false;
};

} // namespace gameui
