// gameui::PlayerListFlow implementation - see PlayerListFlow.h.
#include "PlayerListFlow.h"

#include "../../../panels/playerlist/PlayerListPage.h"

#include <abc/Console.h>

namespace gameui {

PlayerListFlow::PlayerListFlow(PlayerListHost host,
                               services::playerlist::PlayerListModel &model)
    : Host(std::move(host)), Model(model) {}

void PlayerListFlow::setPage(panels::playerlist::PlayerListPage *page) {
  Page = page;
}

void PlayerListFlow::shutdown() {
  Page = nullptr;
  Presented = false;
}

void PlayerListFlow::hide() {
  if (Page)
    Page->hide();
  Presented = false;
}

void PlayerListFlow::present() {
  if (!Page)
    return;
  Model.refresh();
  Page->render();
  Page->show();
  Presented = true;
}

void PlayerListFlow::selectRow(const services::playerlist::SelectionKey &key) {
  Model.refresh();
  Model.select(key);
  if (Page)
    Page->render();
}

void PlayerListFlow::toggleMute() {
  bool mutedAfter = false;
  const auto result = Model.toggleMuted(mutedAfter);
  switch (result) {
  case services::playerlist::ToggleError::kNone:
    abc::ConsolePrint("[AbcUi] player mute %s: idx=%d now=%d\n",
                      mutedAfter ? "MUTED" : "UNMUTED",
                      Model.selectedEntry() ? Model.selectedEntry()->index : 0,
                      mutedAfter ? 1 : 0);
    break;
  case services::playerlist::ToggleError::kUnavailable:
    abc::ConsoleWarn(
        "[AbcUi] player mute unavailable (voice service not initialized)\n");
    break;
  case services::playerlist::ToggleError::kNoSelection:
    abc::ConsoleWarn("[AbcUi] player mute refused (no selection)\n");
    break;
  case services::playerlist::ToggleError::kIdentityChanged:
    abc::ConsoleWarn(
        "[AbcUi] player mute refused (selected player left or is a bot)\n");
    break;
  case services::playerlist::ToggleError::kReadbackFailed:
    abc::ConsoleWarn(
        "[AbcUi] player mute refused (backend did not change state)\n");
    break;
  case services::playerlist::ToggleError::kLocalPlayer:
    abc::ConsoleWarn("[AbcUi] player mute refused (local player)\n");
    break;
  }
  if (Page)
    Page->render();
}

void PlayerListFlow::handleAction(navigation::ActionKind kind) {
  // Page-internal intents: a row selection carries the identity payload;
  // the mute toggle and close are bare intents. Nothing here reaches the
  // engine except through the model's revalidated backend call.
  if (kind == navigation::ActionKind::kPlMute)
    toggleMute();
  else if (kind == navigation::ActionKind::kPlClose)
    Host.dispatchBack();
  else {
    abc::ConsoleWarn("[AbcUi] unknown player-list action \"%s\" dropped\n",
                     navigation::actionName(kind));
    return;
  }
  Host.finishDispatch();
}

} // namespace gameui
