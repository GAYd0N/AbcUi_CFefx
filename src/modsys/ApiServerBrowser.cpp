// framework::ServerBrowserApi - the sdk serverbrowser section (the
// find-servers page's data and intents). See ApiTable.h for how it joins
// the module table.
#include "ApiTable.h"
#include "ApiSnapshot.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "../services/serverbrowser/ServerBlacklist.h"
#include "../services/serverbrowser/ServerBrowserService.h"
#include "../services/serverbrowser/ServerBrowserSession.h"
#include "../services/serverbrowser/ServerFavorites.h"
#include "../services/serverbrowser/ServerListModel.h"
#include "../services/serverbrowser/ServerQuery.h"

namespace framework {
namespace ServerBrowserApi {
namespace {

using services::serverbrowser::QueryFailure;
using services::serverbrowser::QueryKind;
using services::serverbrowser::QueryStatus;
using services::serverbrowser::ServerBlacklist;
using services::serverbrowser::ServerBrowserSession;
using services::serverbrowser::ServerEntry;
using services::serverbrowser::ServerFilter;
using services::serverbrowser::ServerSort;

void copyText(const std::string &text, char *out, uint32_t outSize) {
  if (!out || outSize == 0)
    return;
  uint32_t i = 0;
  for (; i + 1 < outSize && i < text.size(); ++i)
    out[i] = text[i];
  out[i] = '\0';
}

const services::serverbrowser::Access &access() {
  return services::serverbrowser::access();
}

ServerBrowserSession *session() {
  return access().session;
}

ServerBlacklist *blacklist() {
  return access().blacklist;
}

int32_t mapStatus(QueryStatus status) {
  switch (status) {
  case QueryStatus::Querying:
    return ABCUI_SB_STATUS_QUERYING;
  case QueryStatus::Completed:
    return ABCUI_SB_STATUS_COMPLETED;
  case QueryStatus::Cancelled:
    return ABCUI_SB_STATUS_CANCELLED;
  case QueryStatus::Failed:
    return ABCUI_SB_STATUS_FAILED;
  case QueryStatus::Idle:
    break;
  }
  return ABCUI_SB_STATUS_IDLE;
}

int32_t mapFailure(QueryFailure failure) {
  switch (failure) {
  case QueryFailure::Unavailable:
    return ABCUI_SB_FAILURE_UNAVAILABLE;
  case QueryFailure::Rejected:
    return ABCUI_SB_FAILURE_REJECTED;
  case QueryFailure::TimedOut:
    return ABCUI_SB_FAILURE_TIMED_OUT;
  case QueryFailure::MasterNoResponse:
    return ABCUI_SB_FAILURE_MASTER_NO_RESPONSE;
  }
  return ABCUI_SB_FAILURE_MASTER_NO_RESPONSE;
}

int32_t mapTab(QueryKind kind) {
  switch (kind) {
  case QueryKind::Lan:
    return ABCUI_SB_TAB_LAN;
  case QueryKind::Favorites:
    return ABCUI_SB_TAB_FAVORITES;
  case QueryKind::History:
    return ABCUI_SB_TAB_HISTORY;
  case QueryKind::Friends:
    return ABCUI_SB_TAB_FRIENDS;
  case QueryKind::Internet:
    return ABCUI_SB_TAB_INTERNET;
  }
  return ABCUI_SB_TAB_INTERNET;
}

int32_t mapSort(ServerSort sort) {
  switch (sort) {
  case ServerSort::Map:
    return ABCUI_SB_SORT_MAP;
  case ServerSort::Players:
    return ABCUI_SB_SORT_PLAYERS;
  case ServerSort::Ping:
    return ABCUI_SB_SORT_PING;
  case ServerSort::Name:
    return ABCUI_SB_SORT_NAME;
  }
  return ABCUI_SB_SORT_NAME;
}

ServerSort sortOf(int32_t column) {
  switch (column) {
  case ABCUI_SB_SORT_MAP:
    return ServerSort::Map;
  case ABCUI_SB_SORT_PLAYERS:
    return ServerSort::Players;
  case ABCUI_SB_SORT_PING:
    return ServerSort::Ping;
  default:
    return ServerSort::Name;
  }
}

int32_t apiStatus(void) {
  const auto *target = session();
  return target ? mapStatus(target->Query.status()) : ABCUI_SB_STATUS_IDLE;
}

int32_t apiFailure(void) {
  const auto *target = session();
  return target ? mapFailure(target->Query.failure())
                : ABCUI_SB_FAILURE_MASTER_NO_RESPONSE;
}

int32_t apiKind(void) {
  const auto *target = session();
  return target ? mapTab(target->Query.kind()) : ABCUI_SB_TAB_INTERNET;
}

uint32_t apiGeneration(void) {
  const auto *target = session();
  return target ? target->Query.generation() : 0;
}

int32_t apiServerCount(void) {
  const auto *target = session();
  return target ? target->Query.count() : 0;
}

int32_t apiNoServersListed(void) {
  const auto *target = session();
  return target && target->Query.noServersListed() ? 1 : 0;
}

int32_t apiDetailKind(void) {
  const auto *target = session();
  if (!target || target->Query.detailKind() !=
                     services::serverbrowser::DetailKind::Players)
    return ABCUI_SB_DETAIL_OFF;
  return ABCUI_SB_DETAIL_PLAYERS;
}

int32_t apiDetailStatus(void) {
  const auto *target = session();
  return target ? mapStatus(target->Query.detailStatus())
                : ABCUI_SB_STATUS_IDLE;
}

int32_t apiDetailFailed(void) {
  const auto *target = session();
  return target && target->Query.detailFailed() ? 1 : 0;
}

int32_t apiPlayerCount(void) {
  const auto *target = session();
  return target ? target->Query.playerCount() : 0;
}

int32_t apiSync(void) {
  auto *target = session();
  return target && target->List.sync(target->Query) ? 1 : 0;
}

int32_t apiRowCount(void) {
  const auto *target = session();
  return target ? target->List.rowCount() : 0;
}

int32_t apiPage(void) {
  const auto *target = session();
  return target ? target->List.page() : 0;
}

int32_t apiPageCount(void) {
  const auto *target = session();
  return target ? target->List.pageCount() : 0;
}

int32_t apiSortColumn(void) {
  const auto *target = session();
  return target ? mapSort(target->List.sortColumn()) : ABCUI_SB_SORT_NAME;
}

int32_t apiSortAscending(void) {
  const auto *target = session();
  return target && target->List.sortAscending() ? 1 : 0;
}

int32_t apiBlockedOnly(void) {
  const auto *target = session();
  return target && target->List.blockedOnly() ? 1 : 0;
}

int32_t apiEntryAt(int32_t row, AbcUiSbEntry *out) {
  const auto *target = session();
  if (!target || !out || row < 0)
    return 0;
  const int index = target->List.entryIndexAt(row);
  if (index < 0)
    return 0;
  const ServerEntry *entry = target->Query.entry(index);
  if (!entry)
    return 0;

  AbcUiSbEntry full{};
  full.structSize = sizeof(AbcUiSbEntry);
  full.responded = entry->responded ? 1 : 0;
  full.secure = entry->secure ? 1 : 0;
  full.password = entry->password ? 1 : 0;
  full.players = entry->players;
  full.maxPlayers = entry->maxPlayers;
  full.botPlayers = entry->botPlayers;
  full.pingMs = entry->pingMs;
  full.ip = entry->ip;
  full.connectionPort = entry->connectionPort;
  full.queryPort = entry->queryPort;
  copyText(entry->name, full.name, sizeof(full.name));
  copyText(entry->map, full.map, sizeof(full.map));
  copyText(entry->gameDir, full.gameDir, sizeof(full.gameDir));

  // The caller's declared size wins: never write past an older header.
  abi::copySnapshot(out, full);
  return 1;
}

int32_t apiPlayerAt(int32_t index, AbcUiSbPlayer *out) {
  const auto *target = session();
  if (!target || !out || index < 0)
    return 0;
  const auto *player = target->Query.player(index);
  if (!player)
    return 0;
  AbcUiSbPlayer full{};
  full.structSize = sizeof(AbcUiSbPlayer);
  full.score = player->score;
  full.seconds = static_cast<int32_t>(player->seconds);
  copyText(player->name, full.name, sizeof(full.name));
  abi::copySnapshot(out, full);
  return 1;
}

int32_t apiSelectedRow(void) {
  const auto *target = session();
  return target ? target->List.selectedRow() : -1;
}

int32_t apiSelectedFavorite(void) {
  const auto *target = session();
  if (!target)
    return 0;
  const ServerEntry *selected = target->List.selectedEntry(target->Query);
  if (!selected)
    return -1;
  const auto key = services::serverbrowser::favoriteKeyOf(*selected);
  auto *favorites = services::serverbrowser::steamFavorites();
  if (!key.valid() || !favorites)
    return -1;
  return favorites->isFavorite(key.ip, key.connPort, key.queryPort) ? 1 : 0;
}

int32_t apiFavoritesAvailable(void) {
  auto *favorites = services::serverbrowser::steamFavorites();
  return favorites && favorites->available() ? 1 : 0;
}

int32_t apiFilter(AbcUiSbFilter *out) {
  const auto *target = session();
  if (!target || !out)
    return 0;
  const ServerFilter &filter = target->List.filter();
  AbcUiSbFilter full{};
  full.structSize = sizeof(AbcUiSbFilter);
  full.hideEmpty = filter.hideEmpty ? 1 : 0;
  full.hideFull = filter.hideFull ? 1 : 0;
  full.secureOnly = filter.secureOnly ? 1 : 0;
  full.noPassword = filter.NoPassword ? 1 : 0;
  full.insecureOnly = filter.InsecureOnly ? 1 : 0;
  full.hideUnresponsive = filter.HideUnresponsive ? 1 : 0;
  full.maxPing = filter.MaxPing;
  copyText(filter.gameDir, full.gameDir, sizeof(full.gameDir));
  copyText(filter.Name, full.name, sizeof(full.name));
  copyText(filter.Map, full.map, sizeof(full.map));
  abi::copySnapshot(out, full);
  return 1;
}

void apiSelectRow(int32_t row, int32_t requestPlayers) {
  auto *target = session();
  if (!target || row < 0)
    return;
  target->List.selectRow(row);
  // The selection issues the single-slot player query directly on the
  // model (the model's own discipline replaces the previous detail
  // request and drops stale generations).
  if (requestPlayers) {
    if (const ServerEntry *selected = target->List.selectedEntry(target->Query))
      target->Query.requestPlayers(*selected);
  }
  target->List.sync(target->Query);
}

void apiSetSort(int32_t column, int32_t ascending) {
  auto *target = session();
  if (target)
    target->List.setSort(sortOf(column), ascending != 0);
}

int32_t apiPagePrev(void) {
  auto *target = session();
  return target && target->List.pagePrev() ? 1 : 0;
}

int32_t apiPageNext(void) {
  auto *target = session();
  return target && target->List.pageNext() ? 1 : 0;
}

void apiSetFilter(const AbcUiSbFilter *filter) {
  auto *target = session();
  if (!target || !filter || filter->structSize < sizeof(AbcUiSbFilter))
    return;
  ServerFilter updated = target->List.filter();
  updated.hideEmpty = filter->hideEmpty != 0;
  updated.hideFull = filter->hideFull != 0;
  updated.secureOnly = filter->secureOnly != 0;
  updated.NoPassword = filter->noPassword != 0;
  updated.InsecureOnly = filter->insecureOnly != 0;
  updated.HideUnresponsive = filter->hideUnresponsive != 0;
  updated.MaxPing = filter->maxPing > 0 ? filter->maxPing : 0;
  updated.Name.assign(filter->name,
                      strnlen(filter->name, sizeof(filter->name)));
  updated.Map.assign(filter->map, strnlen(filter->map, sizeof(filter->map)));
  // The game identity (gamedir) stays the framework's own fact.
  target->List.setFilter(updated);
}

int32_t apiResetFilters(void) {
  auto *target = session();
  if (!target)
    return 0;
  ServerFilter reset;
  reset.gameDir = target->List.filter().gameDir;
  reset.HideUnresponsive = target->List.filter().HideUnresponsive;
  const bool masterMoved = target->List.filter().hideEmpty ||
                           target->List.filter().hideFull ||
                           target->List.filter().secureOnly;
  target->List.setFilter(reset);
  return masterMoved ? 1 : 0;
}

void apiSetBlockedOnly(int32_t blocked) {
  auto *target = session();
  if (!target)
    return;
  target->List.setBlockedOnly(blocked != 0);
  if (blocked)
    target->Query.cancelDetail(); // the window is the only slot consumer
}

int32_t apiRuleCount(void) {
  const auto *target = blacklist();
  return target ? static_cast<int32_t>(target->rules().size()) : 0;
}

uint32_t apiRuleRevision(void) {
  const auto *target = blacklist();
  return target ? static_cast<uint32_t>(target->revision()) : 0;
}

int32_t apiRuleAt(int32_t index, AbcUiSbRule *out) {
  const auto *target = blacklist();
  if (!target || !out || index < 0 ||
      static_cast<size_t>(index) >= target->rules().size())
    return 0;
  const auto &rule = target->rules()[static_cast<size_t>(index)];
  AbcUiSbRule full{};
  full.structSize = sizeof(AbcUiSbRule);
  full.blockedAt = rule.BlockedAt;
  copyText(rule.Name, full.name, sizeof(full.name));
  copyText(rule.address(), full.address, sizeof(full.address));
  abi::copySnapshot(out, full);
  return 1;
}

int32_t apiBlockSelected(int32_t entireIp) {
  auto *target = session();
  ServerBlacklist *rules = blacklist();
  if (!target || !rules)
    return 0;
  const ServerEntry *selected = target->List.selectedEntry(target->Query);
  if (!selected)
    return 0;
  const uint16_t port = entireIp                   ? 0
                        : selected->connectionPort ? selected->connectionPort
                                                   : selected->queryPort;
  services::serverbrowser::BlacklistRule rule{selected->ip, port};
  return rules->add(rule.address(), selected->name) ? 1 : 0;
}

int32_t apiBlockAddress(const char *address) {
  ServerBlacklist *target = blacklist();
  if (!target || !address || !*address)
    return 0;
  return target->add(address) ? 1 : 0;
}

int32_t apiRemoveRule(const char *address) {
  ServerBlacklist *target = blacklist();
  if (!target || !address || !*address)
    return 0;
  return target->remove(address) ? 1 : 0;
}

void apiSetTab(int32_t kind) {
  switch (kind) {
  case ABCUI_SB_TAB_LAN:
    access().setTab(static_cast<int>(QueryKind::Lan));
    break;
  case ABCUI_SB_TAB_FAVORITES:
    access().setTab(static_cast<int>(QueryKind::Favorites));
    break;
  case ABCUI_SB_TAB_HISTORY:
    access().setTab(static_cast<int>(QueryKind::History));
    break;
  case ABCUI_SB_TAB_FRIENDS:
    access().setTab(static_cast<int>(QueryKind::Friends));
    break;
  default:
    access().setTab(static_cast<int>(QueryKind::Internet));
    break;
  }
}

void apiRefresh(void) {
  if (access().refresh)
    access().refresh();
}

void apiStop(void) {
  if (access().stop)
    access().stop();
}

void apiConnect(void) {
  if (access().connect)
    access().connect();
}

void apiToggleFavorite(void) {
  if (access().toggleFavorite)
    access().toggleFavorite();
}

void apiApplyFilters(void) {
  if (access().applyFilters)
    access().applyFilters();
}

void apiCloseDetail(void) {
  auto *target = session();
  if (target)
    target->Query.cancelDetail();
}

void apiSetAutoRetryJoin(int32_t enabled) {
  auto *target = session();
  if (target)
    target->AutoRetryJoin = enabled != 0;
}

// The ESC layering claim (see noteEscapeClaim).
bool &escapeClaimSlot() {
  static bool claimed = false;
  return claimed;
}

void apiNoteEscapeClaim(void) {
  escapeClaimSlot() = true;
}

const AbcUiServerBrowserApi kServerBrowserApi = {
    sizeof(AbcUiServerBrowserApi),
    apiStatus,
    apiFailure,
    apiKind,
    apiGeneration,
    apiServerCount,
    apiNoServersListed,
    apiDetailKind,
    apiDetailStatus,
    apiDetailFailed,
    apiPlayerCount,
    apiSync,
    apiRowCount,
    apiPage,
    apiPageCount,
    apiSortColumn,
    apiSortAscending,
    apiBlockedOnly,
    apiEntryAt,
    apiPlayerAt,
    apiSelectedRow,
    apiSelectedFavorite,
    apiFavoritesAvailable,
    apiFilter,
    apiSelectRow,
    apiSetSort,
    apiPagePrev,
    apiPageNext,
    apiSetFilter,
    apiResetFilters,
    apiSetBlockedOnly,
    apiRuleCount,
    apiRuleRevision,
    apiRuleAt,
    apiBlockSelected,
    apiBlockAddress,
    apiRemoveRule,
    apiSetTab,
    apiRefresh,
    apiStop,
    apiConnect,
    apiToggleFavorite,
    apiApplyFilters,
    apiCloseDetail,
    apiSetAutoRetryJoin,
    apiNoteEscapeClaim,
};

} // namespace

const AbcUiServerBrowserApi *get() {
  return &kServerBrowserApi;
}

// framework-internal (the menu owner's routed ESC): reads-and-clears the
// browser page's one-subpage claim.
bool consumeEscapeClaim() {
  bool &claimed = escapeClaimSlot();
  const bool value = claimed;
  claimed = false;
  return value;
}

} // namespace ServerBrowserApi
} // namespace framework
