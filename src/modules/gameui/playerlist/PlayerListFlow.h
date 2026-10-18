#pragma once

// gameui::PlayerListFlow - the player-list page and its mute/selection
// intents. Owns the presented latch and the page pointer; the roster model
// and the page object stay owned by GameUiModule and are injected here.

#include "../../../services/playerlist/PlayerListModel.h"
#include "../navigation/MenuAction.h"

#include <functional>

namespace panels::playerlist {
class PlayerListPage;
}

namespace gameui {

struct PlayerListHost {
  std::function<void()> dispatchBack;
  std::function<void()> finishDispatch;
};

class PlayerListFlow {
public:
  PlayerListFlow(PlayerListHost host,
                 services::playerlist::PlayerListModel &model);

  void setPage(panels::playerlist::PlayerListPage *page);
  void shutdown();
  bool presented() const {
    return Presented;
  }
  void setPresented(bool presented) {
    Presented = presented;
  }
  void hide();

  void present();
  void selectRow(const services::playerlist::SelectionKey &key);
  void toggleMute();
  void handleAction(navigation::ActionKind kind);

private:
  PlayerListHost Host;
  services::playerlist::PlayerListModel &Model;
  panels::playerlist::PlayerListPage *Page = nullptr;
  bool Presented = false;
};

} // namespace gameui
