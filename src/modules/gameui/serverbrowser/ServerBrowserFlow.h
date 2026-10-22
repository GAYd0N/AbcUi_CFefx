#pragma once

// gameui::ServerBrowserFlow - the server browser's query lifecycle and
// intent boundaries.
//
// The page became a module (abcui.gameui, sdk serverbrowser section): the
// flow keeps the session ownership (fresh-open filter sampling, the
// released queries on leave), the Steam favorites/history edges and the
// intent boundaries the section maps 1:1. The connect address validation
// and the join-success history record stay here. The session and blacklist
// stay owned by GameUiModule (debug probes use them directly) and are
// injected here; the composition root supplies the connect dispatch,
// finishDispatch and the auto-retry reset.

#include "../../../services/serverbrowser/ServerBlacklist.h"
#include "../../../services/serverbrowser/ServerBrowserSession.h"
#include "../../../services/serverbrowser/ServerFavorites.h"

#include <functional>
#include <string>

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
  // The intent boundaries (the section maps its calls 1:1).
  void applyFiltersQuery();
  void refreshOrStop();
  void stop();

private:
  ServerBrowserHost Host;
  services::serverbrowser::ServerBrowserSession &Session;
  services::serverbrowser::ServerBlacklist &Blacklist;
  services::serverbrowser::FavoriteKey PendingHistory;
  bool PendingHistoryValid = false;
};

} // namespace gameui
