#pragma once

// gameui::ServerBrowserFlow - query lifecycle, connect/favorite intents,
// pending join-history and the page presented latch.
//
// The session and blacklist stay owned by GameUiModule (debug probes and
// page wiring use them directly) and are injected here; the composition root
// supplies the connect dispatch, finishDispatch and the auto-retry reset.

#include "../../../services/serverbrowser/ServerBlacklist.h"
#include "../../../services/serverbrowser/ServerBrowserSession.h"
#include "../../../services/serverbrowser/ServerFavorites.h"
#include "../navigation/MenuAction.h"

#include <functional>
#include <string>

namespace panels::serverbrowser {
class ServerBrowserPage;
}

namespace gameui {

struct ServerBrowserHost {
  std::function<void(const std::string &address)> dispatchConnect;
  std::function<void()> finishDispatch;
  std::function<void()> resetAutoRetry;
};

class ServerBrowserFlow {
public:
  ServerBrowserFlow(ServerBrowserHost host,
                    services::serverbrowser::ServerBrowserSession &session,
                    services::serverbrowser::ServerBlacklist &blacklist);

  void setPage(panels::serverbrowser::ServerBrowserPage *page);
  void shutdown();
  bool presented() const {
    return Session.Presented;
  }
  void setPresented(bool presented) {
    Session.Presented = presented;
  }
  void hide();

  void present();
  void beginQuery(services::serverbrowser::QueryKind kind);
  void connectSelected();
  void toggleSelectedFavorite();
  void releaseQueries();
  void logQueryChange();
  void recordPendingHistory();
  void discardPendingHistory();
  void handleAction(navigation::ActionKind kind);

private:
  ServerBrowserHost Host;
  services::serverbrowser::ServerBrowserSession &Session;
  services::serverbrowser::ServerBlacklist &Blacklist;
  panels::serverbrowser::ServerBrowserPage *Page = nullptr;
  services::serverbrowser::FavoriteKey PendingHistory;
  bool PendingHistoryValid = false;
};

} // namespace gameui
