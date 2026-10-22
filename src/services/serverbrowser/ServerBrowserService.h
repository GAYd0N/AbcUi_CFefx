#pragma once

// services::serverbrowser::access - the sdk serverbrowser section's access
// point: the process-lifetime query session (query + list view model + the
// auto-retry toggle the page owns) plus the intents' engine edges. The
// Steam favorites/history stores and the blacklist stay where the
// composition root assembled them; the section reads this point only.

namespace services {
namespace serverbrowser {

class ServerBrowserSession;
class ServerBlacklist;

struct Access {
  ServerBrowserSession *session = nullptr;
  ServerBlacklist *blacklist = nullptr;
  // Intents (the flow's own boundaries; the section maps them 1:1).
  void (*setTab)(int kind) = nullptr;
  void (*refresh)() = nullptr;
  void (*stop)() = nullptr;
  void (*connect)() = nullptr;
  void (*toggleFavorite)() = nullptr;
  void (*applyFilters)() = nullptr;
};

void setAccess(const Access &access);
const Access &access();

} // namespace serverbrowser
} // namespace services
