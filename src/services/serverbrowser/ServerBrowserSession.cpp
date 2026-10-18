// ServerBrowserSession lifecycle implementation; no engine or DOM dependency.
#include "ServerBrowserSession.h"

namespace services::serverbrowser {

void ServerBrowserSession::startContext() {
  closeContext();
  const ServerFilter Filter = List.filter();
  const ServerSort Sort = List.sortColumn();
  const bool Ascending = List.sortAscending();
  Query = ServerQuery{};
  Query.setProvider(Backend);
  List = ServerListModel{};
  List.setBlacklist(Blacklist);
  List.setFilter(Filter);
  List.setSort(Sort, Ascending);
}

void ServerBrowserSession::closeContext() {
  Query.shutdown();
  Presented = false;
  Open = false;
  LastStatus = QueryStatus::Idle;
}

} // namespace services::serverbrowser
