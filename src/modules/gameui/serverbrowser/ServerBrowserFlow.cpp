// gameui::ServerBrowserFlow implementation - see ServerBrowserFlow.h.
#include "ServerBrowserFlow.h"

#include "../../../services/loading/LoadingModel.h"
#include "../../../services/serverbrowser/ServerFavorites.h"

#include <abc/Console.h>
#include <abc/CvarCmd.h>

namespace gameui {

ServerBrowserFlow::ServerBrowserFlow(
    ServerBrowserHost host,
    services::serverbrowser::ServerBrowserSession &session,
    services::serverbrowser::ServerBlacklist &blacklist)
    : Host(std::move(host)), Session(session), Blacklist(blacklist) {}

void ServerBrowserFlow::shutdown() {
  PendingHistoryValid = false;
  Session.Presented = false;
}

void ServerBrowserFlow::hide() {
  Session.Presented = false;
}

void ServerBrowserFlow::present() {
  if (!Session.Presented) {
    // Fresh opening: the cvar-owned unresponsive rule is sampled again
    // (the page module never reads cvars itself). The subpage state is the
    // module's own; its document presents from menu.state.
    auto filter = Session.List.filter();
    filter.HideUnresponsive =
        abc::CVAR_GET_FLOAT("abc_ui_browser_hide_unresponsive") != 0;
    Session.List.setFilter(filter);
  }
  Session.Presented = true;
}

void ServerBrowserFlow::beginQuery(services::serverbrowser::QueryKind kind) {
  if (Session.Query.closed())
    return;
  services::serverbrowser::FilterPair pairs[8];
  const int count = Session.List.filter().buildPairs(pairs, 8);
  Session.Query.begin(kind, count > 0 ? pairs : nullptr, count);
  logQueryChange();
}

void ServerBrowserFlow::connectSelected() {
  const auto *selected = Session.List.selectedEntry(Session.Query);
  if (!selected || !selected->responded) {
    abc::ConsoleWarn(
        "[AbcUi] server connect refused (no responded selection)\n");
    return;
  }
  if (Blacklist.contains(selected->ip, selected->connectionPort
                                           ? selected->connectionPort
                                           : selected->queryPort)) {
    abc::ConsoleWarn("[AbcUi] blocked server must be removed from the "
                     "blacklist before connecting\n");
    return;
  }
  const std::string address =
      services::serverbrowser::buildConnectAddress(*selected);
  std::string host;
  int port = 0;
  if (address.empty() || !services::loading::LoadingModel::parseAddress(
                             address.c_str(), host, port)) {
    abc::ConsoleWarn(
        "[AbcUi] server connect refused (unrepresentable address)\n");
    return;
  }
  // The join-success memory entry (the shipped ConnectedToGame records
  // flags=2 into Steam's own store; see ServerFavorites.h). Kept for the
  // live attempt only - a cancel/failure retires it.
  PendingHistory = services::serverbrowser::favoriteKeyOf(*selected);
  PendingHistoryValid = PendingHistory.valid();
  // A user-initiated join starts a fresh auto-retry chain.
  Host.resetAutoRetry();
  Host.dispatchConnect(address);
  Host.finishDispatch();
}

void ServerBrowserFlow::toggleSelectedFavorite() {
  const auto *selected = Session.List.selectedEntry(Session.Query);
  if (!selected) {
    abc::ConsoleWarn("[AbcUi] favorite toggle refused (no selection)\n");
    return;
  }
  auto *favorites = services::serverbrowser::steamFavorites();
  const auto key = services::serverbrowser::favoriteKeyOf(*selected);
  if (!key.valid()) {
    abc::ConsoleWarn(
        "[AbcUi] favorite toggle refused (unrepresentable address)\n");
    return;
  }
  const bool wasFavorite =
      favorites->isFavorite(key.ip, key.connPort, key.queryPort);
  const bool ok =
      wasFavorite
          ? favorites->removeFavorite(key.ip, key.connPort, key.queryPort)
          : favorites->addFavorite(key.ip, key.connPort, key.queryPort);
  abc::ConsolePrint("[AbcUi] favorite %s \"%s\": %s (count=%d)\n",
                    wasFavorite ? "remove" : "add", selected->name.c_str(),
                    ok ? "OK" : "REFUSED", favorites->favoriteCount());
  // The page module re-projects the button state from the store (the
  // signature mixes the read-through favorite state).
}

void ServerBrowserFlow::releaseQueries() {
  Session.Query.cancelDetail();
  Session.Query.release();
  Session.Open = false;
}

void ServerBrowserFlow::logQueryChange() {
  using services::serverbrowser::QueryStatus;
  const QueryStatus status = Session.Query.status();
  if (status == Session.LastStatus)
    return;
  Session.LastStatus = status;
  switch (status) {
  case QueryStatus::Querying:
    abc::ConsolePrint("[AbcUi] server query started: kind=%d gen=%u\n",
                      static_cast<int>(Session.Query.kind()),
                      Session.Query.generation());
    break;
  case QueryStatus::Completed:
    abc::ConsolePrint(
        "[AbcUi] server query completed: gen=%u count=%d noServers=%d\n",
        Session.Query.generation(), Session.Query.count(),
        Session.Query.noServersListed() ? 1 : 0);
    break;
  case QueryStatus::Cancelled:
    abc::ConsolePrint("[AbcUi] server query cancelled: gen=%u partial=%d\n",
                      Session.Query.generation(), Session.Query.count());
    break;
  case QueryStatus::Failed:
    abc::ConsolePrint("[AbcUi] server query failed: gen=%u failure=%d\n",
                      Session.Query.generation(),
                      static_cast<int>(Session.Query.failure()));
    break;
  default:
    break;
  }
}

void ServerBrowserFlow::discardPendingHistory() {
  PendingHistoryValid = false;
}

void ServerBrowserFlow::recordPendingHistory() {
  if (!PendingHistoryValid)
    return;
  PendingHistoryValid = false;
  auto *favorites = services::serverbrowser::steamFavorites();
  const bool ok = favorites->addHistory(
      PendingHistory.ip, PendingHistory.connPort, PendingHistory.queryPort);
  abc::ConsolePrint("[AbcUi] history record %u.%u.%u.%u:%u: %s\n",
                    PendingHistory.ip >> 24, (PendingHistory.ip >> 16) & 0xff,
                    (PendingHistory.ip >> 8) & 0xff, PendingHistory.ip & 0xff,
                    PendingHistory.connPort, ok ? "OK" : "FAILED");
}

void ServerBrowserFlow::applyFiltersQuery() {
  // The master-side pair moved: the list query is re-issued with the same
  // kind (the module's filter toggles report the change).
  beginQuery(Session.Query.kind());
}

void ServerBrowserFlow::refreshOrStop() {
  if (Session.Query.listRequest() != 0)
    Session.Query.refreshAll();
  else
    beginQuery(Session.Query.kind());
  logQueryChange();
}

void ServerBrowserFlow::stop() {
  Session.Query.cancel();
  logQueryChange();
}

} // namespace gameui
