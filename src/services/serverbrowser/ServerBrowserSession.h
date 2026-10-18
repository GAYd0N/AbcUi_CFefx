#pragma once

// Context-scoped query ownership. The provider outlives the session; each
// context gets a fresh terminal query object and a fresh projection cache.
// Keeping this protocol outside DOM assembly makes recreation testable.
#include "ServerQuery.h"
#include "ServerListModel.h"

namespace services::serverbrowser {

class ServerBrowserSession {
public:
  ServerBrowserSession() = default;
  ServerBrowserSession(const ServerBrowserSession &) = delete;
  ServerBrowserSession &operator=(const ServerBrowserSession &) = delete;
  void setProvider(IServerQueryProvider *Provider) {
    Backend = Provider;
  }
  void setBlacklist(ServerBlacklist *Value) {
    Blacklist = Value;
    List.setBlacklist(Value);
  }
  void startContext();
  void closeContext();

  ServerQuery Query;
  ServerListModel List;
  bool Presented = false;
  bool Open = false;
  QueryStatus LastStatus = QueryStatus::Idle;

private:
  IServerQueryProvider *Backend = nullptr;
  ServerBlacklist *Blacklist = nullptr;
};

} // namespace services::serverbrowser
