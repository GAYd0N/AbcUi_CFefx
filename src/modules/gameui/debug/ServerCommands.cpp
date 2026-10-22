// Debug-only server commands; extracted without changing command names.
#ifndef NDEBUG
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/GameUiPages.h"
#include "DebugCommands.h"
#include <abc/Console.h>
#include <abc/Plugin.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
namespace gameui {
void DebugCommands::serverQueryCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  if (gEngfuncs.Cmd_Argc() < 2) {
    abc::ConsoleWarn(
        "[AbcUi] usage: abc_ui_server_query "
        "<internet|lan|favorites|history|spectate|friends> [key value ...]\n");
    return;
  }
  const std::string kindName = gEngfuncs.Cmd_Argv(1);
  services::serverbrowser::QueryKind kind;
  if (kindName == "internet")
    kind = services::serverbrowser::QueryKind::Internet;
  else if (kindName == "lan")
    kind = services::serverbrowser::QueryKind::Lan;
  else if (kindName == "favorites")
    kind = services::serverbrowser::QueryKind::Favorites;
  else if (kindName == "history")
    kind = services::serverbrowser::QueryKind::History;
  else if (kindName == "spectate")
    kind = services::serverbrowser::QueryKind::Spectate;
  else if (kindName == "friends")
    kind = services::serverbrowser::QueryKind::Friends;
  else {
    abc::ConsoleWarn("[AbcUi] unknown query kind \"%s\"\n", kindName.c_str());
    return;
  }
  std::vector<services::serverbrowser::FilterPair> filters;
  for (int i = 2; i + 1 < gEngfuncs.Cmd_Argc(); i += 2)
    filters.push_back({gEngfuncs.Cmd_Argv(i), gEngfuncs.Cmd_Argv(i + 1)});
  self.ServerBrowser.Open =
      true; // diagnostic opens the query context explicitly
  const bool started = self.ServerBrowser.Query.begin(
      kind, filters.empty() ? nullptr : filters.data(),
      static_cast<int>(filters.size()));
  abc::ConsolePrint(
      "[AbcUi] server query %s: kind=%s gen=%u request=%llu\n",
      started ? "STARTED" : "FAILED", kindName.c_str(),
      self.ServerBrowser.Query.generation(),
      static_cast<unsigned long long>(self.ServerBrowser.Query.listRequest()));
  serverStatusCommand();
}

void DebugCommands::serverStatusCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  auto &query = self.ServerBrowser.Query;
  abc::ConsolePrint("[AbcUi] server: status=%d failure=%d gen=%u count=%d "
                    "listing=%llu noServers=%d stale=%llu dropped=%d "
                    "failedPings=%d closed=%d pageOpen=%d\n",
                    static_cast<int>(query.status()),
                    static_cast<int>(query.failure()), query.generation(),
                    query.count(),
                    static_cast<unsigned long long>(query.listRequest()),
                    query.noServersListed() ? 1 : 0,
                    static_cast<unsigned long long>(query.staleEvents()),
                    query.droppedServers(), query.failedPings(),
                    query.closed() ? 1 : 0, self.ServerBrowser.Open ? 1 : 0);
  abc::ConsolePrint("[AbcUi] server: detail=%d detailStatus=%d detailFailed=%d "
                    "players=%d handle=%llu outstanding=%d\n",
                    static_cast<int>(query.detailKind()),
                    static_cast<int>(query.detailStatus()),
                    query.detailFailed() ? 1 : 0, query.playerCount(),
                    static_cast<unsigned long long>(query.detailRequest()),
                    query.providerOutstanding());
  const int shown = query.count() < 10 ? query.count() : 10;
  for (int i = 0; i < shown; ++i) {
    const auto *entry = query.entry(i);
    if (!entry)
      continue;
    abc::ConsolePrint("[AbcUi] server[%d]: name=\"%s\" map=\"%s\" "
                      "players=%d/%d bots=%d ping=%d password=%d secure=%d\n",
                      i, entry->name.c_str(), entry->map.c_str(),
                      entry->players, entry->maxPlayers, entry->botPlayers,
                      entry->pingMs, entry->password ? 1 : 0,
                      entry->secure ? 1 : 0);
  }
}

void DebugCommands::serverCancelCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  currentModule()->ServerBrowser.Query.cancel();
  abc::ConsolePrint("[AbcUi] server cancel issued\n");
  serverStatusCommand();
}

void DebugCommands::serverReleaseCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  currentModule()->Server->releaseQueries();
  abc::ConsolePrint("[AbcUi] server release issued\n");
  serverStatusCommand();
}

void DebugCommands::serverDetailCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  if (gEngfuncs.Cmd_Argc() < 3) {
    abc::ConsoleWarn(
        "[AbcUi] usage: abc_ui_server_detail <ping|players> <index>\n");
    return;
  }
  const std::string kind = gEngfuncs.Cmd_Argv(1);
  const int index = atoi(gEngfuncs.Cmd_Argv(2));
  const auto *server = self.ServerBrowser.Query.entry(index);
  if (!server) {
    abc::ConsoleWarn("[AbcUi] no server at index %d\n", index);
    return;
  }
  uint64_t handle = 0;
  if (kind == "ping")
    handle = self.ServerBrowser.Query.requestPing(*server);
  else if (kind == "players")
    handle = self.ServerBrowser.Query.requestPlayers(*server);
  else {
    abc::ConsoleWarn("[AbcUi] unknown detail kind \"%s\"\n", kind.c_str());
    return;
  }
  abc::ConsolePrint("[AbcUi] server detail %s %s: handle=%llu\n", kind.c_str(),
                    handle ? "STARTED" : "FAILED",
                    static_cast<unsigned long long>(handle));
  serverStatusCommand();
}

void DebugCommands::serverDetailCancelCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  currentModule()->ServerBrowser.Query.cancelDetail();
  abc::ConsolePrint("[AbcUi] server detail cancel issued\n");
  serverStatusCommand();
}

void DebugCommands::serverRefreshCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  if (self.ServerBrowser.Query.listRequest() == 0) {
    abc::ConsoleWarn("[AbcUi] no live request to refresh\n");
    return;
  }
  self.ServerBrowser.Query.refreshAll();
  abc::ConsolePrint("[AbcUi] server refresh issued\n");
  serverStatusCommand();
}

void DebugCommands::serverFavoriteCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  auto *favorites = services::serverbrowser::steamFavorites();
  abc::ConsolePrint(
      "[AbcUi] favorites: available=%d favCount=%d histCount=%d\n",
      favorites->available() ? 1 : 0, favorites->favoriteCount(),
      favorites->historyCount());
  const auto *selected =
      self.ServerBrowser.List.selectedEntry(self.ServerBrowser.Query);
  if (!selected) {
    abc::ConsoleWarn(
        "[AbcUi] favorites: no selection (use abc_ui_server_select first)\n");
    return;
  }
  const auto key = services::serverbrowser::favoriteKeyOf(*selected);
  abc::ConsolePrint(
      "[AbcUi] favorites: selected=\"%s\" key=%u.%u.%u.%u:%u/%u favorited=%d\n",
      selected->name.c_str(), key.ip >> 24, (key.ip >> 16) & 0xff,
      (key.ip >> 8) & 0xff, key.ip & 0xff, key.connPort, key.queryPort,
      favorites->isFavorite(key.ip, key.connPort, key.queryPort) ? 1 : 0);
}

void DebugCommands::serverHistoryCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  if (gEngfuncs.Cmd_Argc() < 4) {
    abc::ConsoleWarn(
        "[AbcUi] usage: abc_ui_server_history <ip> <connPort> <queryPort>\n");
    return;
  }
  // ip accepts dotted quads (the same text form the UI shows).
  unsigned a = 0, b = 0, c = 0, d = 0;
  if (std::sscanf(gEngfuncs.Cmd_Argv(1), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
    abc::ConsoleWarn("[AbcUi] bad ip\n");
    return;
  }
  const uint32_t ip = (a << 24) | (b << 16) | (c << 8) | d;
  const uint16_t conn = static_cast<uint16_t>(atoi(gEngfuncs.Cmd_Argv(2)));
  const uint16_t query = static_cast<uint16_t>(atoi(gEngfuncs.Cmd_Argv(3)));
  auto *favorites = services::serverbrowser::steamFavorites();
  const bool ok = favorites->addHistory(ip, conn, query);
  abc::ConsolePrint("[AbcUi] history %u.%u.%u.%u:%u/%u: %s (histCount=%d)\n", a,
                    b, c, d, conn, query, ok ? "OK" : "FAILED",
                    favorites->historyCount());
}

void DebugCommands::serverConnectCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  const auto *selected =
      self.ServerBrowser.List.selectedEntry(self.ServerBrowser.Query);
  if (!selected) {
    abc::ConsoleWarn("[AbcUi] connect: no selection\n");
    return;
  }
  self.Server->connectSelected();
  abc::ConsolePrint("[AbcUi] connect issued: phase=%d addr=\"%s\"\n",
                    static_cast<int>(self.Controller.state().phase),
                    self.Controller.state().connectAddress.c_str());
}

void DebugCommands::serverTimeoutCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  if (gEngfuncs.Cmd_Argc() < 2) {
    abc::ConsoleWarn("[AbcUi] usage: abc_ui_server_timeout <ms|0>\n");
    return;
  }
  const int ms = atoi(gEngfuncs.Cmd_Argv(1));
  self.ServerBrowser.Query.setTimeoutMs(
      ms > 0 ? static_cast<uint32_t>(ms)
             : services::serverbrowser::kDefaultTimeoutMs);
  abc::ConsolePrint("[AbcUi] server timeout set to %d ms\n",
                    ms > 0 ? ms : 30000);
}

void DebugCommands::serverSelectCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  if (gEngfuncs.Cmd_Argc() < 2) {
    abc::ConsoleWarn("[AbcUi] usage: abc_ui_server_select <row>\n");
    return;
  }
  GameUiModule &self = *currentModule();
  const int row = atoi(gEngfuncs.Cmd_Argv(1));
  self.ServerBrowser.List.selectRow(row);
  const auto *selected =
      self.ServerBrowser.List.selectedEntry(self.ServerBrowser.Query);
  if (!selected) {
    abc::ConsoleWarn("[AbcUi] no server at row %d\n", row);
    return;
  }
  const uint64_t handle = self.ServerBrowser.Query.requestPlayers(*selected);
  abc::ConsolePrint(
      "[AbcUi] server select row=%d key=%llu name=\"%s\" detail=%s\n", row,
      static_cast<unsigned long long>(self.ServerBrowser.List.selectedKey()),
      selected->name.c_str(), handle ? "STARTED" : "FAILED");
}

void DebugCommands::serverUiCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  // The browser DOM moved into the abcui.gameui module; this dumps the
  // framework-side query snapshot instead.
  GameUiModule &self = *currentModule();
  auto &query = self.ServerBrowser.Query;
  abc::ConsolePrint(
      "[AbcUi] browser-ui: status=%d kind=%d gen=%u count=%d rows=%d "
      "page=%d/%d\n",
      static_cast<int>(query.status()), static_cast<int>(query.kind()),
      query.generation(), query.count(), self.ServerBrowser.List.rowCount(),
      self.ServerBrowser.List.page() + 1, self.ServerBrowser.List.pageCount());
}

} // namespace gameui
#endif
