#pragma once

// gameui::VoteMenuFlow - vote-creation page, request latch and submit path.
//
// Owns the VoteMenuModel and the page pointer; the composition root supplies
// the navigation callbacks (ensure the menu is up, reconcile to in-game,
// dispatch open/back, finish the dispatch).

#include "../../../services/votemenu/VoteMenuModel.h"
#include "../navigation/MenuAction.h"

#include <functional>
#include <string>
#include <vector>

namespace panels::votemenu {
class VoteMenuPage;
}

namespace gameui {

struct VoteMenuHost {
  std::function<bool()> shellReady;
  std::function<bool()> ensureMenu;
  std::function<bool()> reconcileInGame;
  std::function<bool()> pageOnTop;
  std::function<void()> dispatchOpen;
  std::function<void()> dispatchBack;
  std::function<void()> finishDispatch;
};

class VoteMenuFlow {
public:
  VoteMenuFlow(VoteMenuHost host,
               services::votemenu::VoteMenuModel::Seams seams);

  void setPage(panels::votemenu::VoteMenuPage *page);
  void shutdown();
  bool presented() const {
    return Presented;
  }
  void setPresented(bool presented) {
    Presented = presented;
  }
  bool consumeRequest();
  void resetForWorld();
  void hide();

  void open();
  void present();
  void tick();
  void applyMapList(int clearAll, int allCount, int start, int end,
                    const std::vector<std::string> &maps);
  void handleAction(navigation::ActionKind kind);

  services::votemenu::VoteMenuModel &model() {
    return Model;
  }

private:
  VoteMenuHost Host;
  services::votemenu::VoteMenuModel Model;
  panels::votemenu::VoteMenuPage *Page = nullptr;
  bool Presented = false;
  bool Requested = false;
};

} // namespace gameui
