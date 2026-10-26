// Direct Steam server discovery, details, favorites and history through the
// host-owned steam_api.dll. RuntimeAccess resolves SDK-versioned interfaces
// via SteamAPI_GetHSteamUser / SteamInternal_FindOrCreateUserInterface.
// The engine owns initialization, shutdown and callback pumping.
// AppID comes from ISteamUtils, with the engine's steam_appid.txt as fallback.
//
// Threading: all provider API below runs on the owner (main) thread. Steam
// list callbacks arrive from SteamAPI_RunCallbacks, which the engine calls on
// its own thread; the response objects therefore only ever push owned copies
// into the mutex-guarded event queue. The response object for a request lives
// until ReleaseRequest/CancelServerQuery returns (Steam's documented
// "no callback after release" contract), which is exactly the T17 "destroy
// only after the backend confirms silence" rule.

#include "ServerQuery.h"
#include "ServerFavorites.h"

#include "../steam/RuntimeAccess.h"

#include <steam/isteammatchmaking.h>
#include <steam/isteamutils.h>

#include <abc/Parse.h>

#include <fstream>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace services::serverbrowser {

namespace {

constexpr int kMaxQueuedEvents = 4096;

struct Bindings {
  ISteamMatchmakingServers *servers = nullptr;
  ISteamMatchmaking *matchmaking = nullptr; // "SteamMatchMaking009" (favorites)
  ISteamUtils *utils = nullptr;
  uint32_t appId = 0;
};

Bindings &bindings() {
  static Bindings instance;
  return instance;
}

// Engine-guaranteed fallback: the host writes the running AppID here before
// SteamAPI_Init (hw.dll 0x01db3910). Read-only; anything unparsable = 0.
uint32_t readSteamAppIdFile() {
  std::ifstream file("steam_appid.txt", std::ios::binary);
  if (!file)
    return 0;
  char buffer[32] = {};
  file.read(buffer, sizeof(buffer) - 1);
  const auto read = file.gcount();
  if (read == 0)
    return 0;
  const auto value = abc::ParseInt(buffer);
  if (!value || *value <= 0 || *value > 0xffffff)
    return 0;
  return static_cast<uint32_t>(*value);
}

// The shared provider -> model queue. Bounded; overflow drops the newest
// delivery and counts it (list completeness matters more than recency, so a
// full queue is an anomaly the diagnostics surface, never a silent reorder).
struct EventQueue {
  std::mutex mutex;
  std::deque<QueryEvent> events;
  uint64_t dropped = 0;

  void push(QueryEvent &&event) {
    std::lock_guard lock(mutex);
    if (static_cast<int>(events.size()) >= kMaxQueuedEvents) {
      ++dropped;
      return;
    }
    events.push_back(std::move(event));
  }
};

EventQueue &queue() {
  static EventQueue instance;
  return instance;
}

// The shared lazy binding (module probe -> hSteamUser -> interfaces). The
// favorites store and the query provider need overlapping sets; the query
// provider additionally requires the list interface. Never calls
// Init/Shutdown/RunCallbacks - the engine owns those (T17a evidence).
bool ensureBindings(bool requireServers) {
  Bindings &b = bindings();
  b.servers = static_cast<ISteamMatchmakingServers *>(
      services::steam::findUserInterface(
          STEAMMATCHMAKINGSERVERS_INTERFACE_VERSION));
  b.matchmaking = static_cast<ISteamMatchmaking *>(
      services::steam::findUserInterface(STEAMMATCHMAKING_INTERFACE_VERSION));
  b.utils = static_cast<ISteamUtils *>(
      services::steam::findUserInterface("SteamUtils009"));
  if (!b.utils)
    b.utils = static_cast<ISteamUtils *>(
        services::steam::findUserInterface(STEAMUTILS_INTERFACE_VERSION));
  if (!b.appId && b.utils)
    b.appId = b.utils->GetAppID();
  if (!b.appId)
    b.appId = readSteamAppIdFile();
  if (requireServers && !b.servers)
    return false;
  return b.appId != 0;
}

// One list request's response sink. Steam guarantees no callback after
// ReleaseRequest, so the object is destroyed together with the mapping entry
// that owns it.
class ListResponse final : public ISteamMatchmakingServerListResponse {
public:
  explicit ListResponse(uint64_t tag) : Tag(tag) {}

  void ServerResponded(HServerListRequest, int iServer) override {
    QueryEvent event;
    event.type = QueryEvent::Type::ServerResponded;
    event.request = Tag;
    event.index = iServer;
    queue().push(std::move(event));
  }

  void ServerFailedToRespond(HServerListRequest, int iServer) override {
    QueryEvent event;
    event.type = QueryEvent::Type::ServerFailedToRespond;
    event.request = Tag;
    event.index = iServer;
    queue().push(std::move(event));
  }

  void RefreshComplete(HServerListRequest,
                       EMatchMakingServerResponse response) override {
    QueryEvent event;
    event.type = QueryEvent::Type::RefreshComplete;
    event.request = Tag;
    event.response = static_cast<int>(response);
    queue().push(std::move(event));
  }

private:
  uint64_t Tag;
};

// Single-server ping sink (ISteamMatchmakingPingResponse). CancelServerQuery
// guarantees silence afterwards, so the object dies with its mapping entry.
class PingResponse final : public ISteamMatchmakingPingResponse {
public:
  explicit PingResponse(uint64_t tag) : Tag(tag) {}

  void ServerResponded(gameserveritem_t &server) override {
    QueryEvent event;
    event.type = QueryEvent::Type::PingResponded;
    event.request = Tag;
    event.server.name = server.GetName();
    event.server.map = server.m_szMap;
    event.server.gameDir = server.m_szGameDir;
    event.server.AppId = server.m_nAppID;
    event.server.gameDescription = server.m_szGameDescription;
    event.server.gameTags = server.m_szGameTags;
    event.server.ip = server.m_NetAdr.GetIP();
    event.server.queryPort = server.m_NetAdr.GetQueryPort();
    event.server.connectionPort = server.m_NetAdr.GetConnectionPort();
    event.server.pingMs = server.m_nPing;
    event.server.players = server.m_nPlayers;
    event.server.maxPlayers = server.m_nMaxPlayers;
    event.server.botPlayers = server.m_nBotPlayers;
    event.server.password = server.m_bPassword;
    event.server.secure = server.m_bSecure;
    event.server.steamId = server.m_steamID.ConvertToUint64();
    event.server.responded = true;
    queue().push(std::move(event));
  }

  void ServerFailedToRespond() override {
    QueryEvent event;
    event.type = QueryEvent::Type::PingFailedToRespond;
    event.request = Tag;
    event.failed = true;
    queue().push(std::move(event));
  }

private:
  uint64_t Tag;
};

class PlayersResponse final : public ISteamMatchmakingPlayersResponse {
public:
  explicit PlayersResponse(uint64_t tag) : Tag(tag) {}

  void AddPlayerToList(const char *name, int score, float timePlayed) override {
    QueryEvent event;
    event.type = QueryEvent::Type::Players;
    event.request = Tag;
    event.players.push_back(PlayerLine{name ? name : "", score, timePlayed});
    queue().push(std::move(event));
  }

  void PlayersFailedToRespond() override {
    QueryEvent event;
    event.type = QueryEvent::Type::PlayersComplete;
    event.request = Tag;
    event.failed = true;
    queue().push(std::move(event));
  }

  void PlayersRefreshComplete() override {
    QueryEvent event;
    event.type = QueryEvent::Type::PlayersComplete;
    event.request = Tag;
    queue().push(std::move(event));
  }

private:
  uint64_t Tag;
};

struct ActiveList {
  HServerListRequest handle = nullptr;
  std::unique_ptr<ListResponse> response;
};

struct ActivePing {
  HServerQuery handle = HSERVERQUERY_INVALID;
  std::unique_ptr<PingResponse> response;
};

struct ActivePlayers {
  HServerQuery handle = HSERVERQUERY_INVALID;
  std::unique_ptr<PlayersResponse> response;
};

class SteamQueryProvider final : public IServerQueryProvider {
public:
  // --- interface binding --------------------------------------------------

  bool ensureBound() {
    return ensureBindings(true);
  }

  bool available() override {
    return ensureBound();
  }

  uint32_t appId() const override {
    return bindings().utils ? bindings().utils->GetAppID() : 0;
  }

  // --- list lifecycle -----------------------------------------------------

  uint64_t requestList(QueryKind kind, const FilterPair *filters,
                       int filterCount) override {
    if (!ensureBound())
      return 0;
    Bindings &b = bindings();
    // One live list request per provider. ServerQuery::begin() cancels and
    // releases its own handle before asking again, so a non-empty list here
    // belongs to another live model: rejecting the new request is the only
    // option that does not release a handle this call does not own (the
    // provider is a process-lifetime singleton).
    if (!Lists.empty())
      return 0;

    // Filter array (Steam reads it synchronously inside the request call).
    // The interface signature takes a non-const MatchMakingKeyValuePair_t**
    // (SDK 1.51 header shape) - the pairs stay local and fully owned here.
    MatchMakingKeyValuePair_t stackPairs[32];
    MatchMakingKeyValuePair_t *pairPointers[32];
    // begin() rejects oversized lists; keep a defensive provider-level
    // clamp for direct callers, never a silent different query.
    int count = filterCount;
    if (count < 0)
      count = 0;
    if (count > 32)
      count = 32;
    for (int i = 0; i < count; ++i) {
      stackPairs[i] = MatchMakingKeyValuePair_t(filters[i].key.c_str(),
                                                filters[i].value.c_str());
      pairPointers[i] = &stackPairs[i];
    }

    const uint64_t tag = NextTag++;
    auto response = std::make_unique<ListResponse>(tag);
    HServerListRequest handle = nullptr;
    switch (kind) {
    case QueryKind::Internet:
      handle = b.servers->RequestInternetServerList(
          b.appId, count ? pairPointers : nullptr, static_cast<uint32>(count),
          response.get());
      break;
    case QueryKind::Lan:
      handle = b.servers->RequestLANServerList(b.appId, response.get());
      break;
    case QueryKind::Favorites:
      handle = b.servers->RequestFavoritesServerList(
          b.appId, count ? pairPointers : nullptr, static_cast<uint32>(count),
          response.get());
      break;
    case QueryKind::History:
      handle = b.servers->RequestHistoryServerList(
          b.appId, count ? pairPointers : nullptr, static_cast<uint32>(count),
          response.get());
      break;
    case QueryKind::Spectate:
      handle = b.servers->RequestSpectatorServerList(
          b.appId, count ? pairPointers : nullptr, static_cast<uint32>(count),
          response.get());
      break;
    case QueryKind::Friends:
      handle = b.servers->RequestFriendsServerList(
          b.appId, count ? pairPointers : nullptr, static_cast<uint32>(count),
          response.get());
      break;
    }
    if (!handle)
      return 0; // response dies here: nothing ever dispatches to it
    ActiveList active;
    active.handle = handle;
    active.response = std::move(response);
    Lists.emplace(tag, std::move(active));
    return tag;
  }

  void cancelQuery(uint64_t request) override {
    Bindings &b = bindings();
    const auto it = Lists.find(request);
    if (it == Lists.end() || !b.servers)
      return;
    if (it->second.handle)
      b.servers->CancelQuery(it->second.handle);
  }

  void releaseRequest(uint64_t request) override {
    Bindings &b = bindings();
    const auto it = Lists.find(request);
    if (it == Lists.end())
      return;
    if (b.servers && it->second.handle)
      b.servers->ReleaseRequest(it->second.handle);
    // Steam: "RefreshComplete callback is not posted when request is
    // released" - the response object is provably quiet, destroy now.
    Lists.erase(it);
  }

  void refreshQueryAll(uint64_t request) override {
    Bindings &b = bindings();
    const auto it = Lists.find(request);
    if (it != Lists.end() && b.servers && it->second.handle)
      b.servers->RefreshQuery(it->second.handle);
  }

  void refreshServer(uint64_t request, int index) override {
    Bindings &b = bindings();
    const auto it = Lists.find(request);
    if (it != Lists.end() && b.servers && it->second.handle)
      b.servers->RefreshServer(it->second.handle, index);
  }

  int serverCount(uint64_t request) override {
    Bindings &b = bindings();
    const auto it = Lists.find(request);
    if (it == Lists.end() || !b.servers || !it->second.handle)
      return 0;
    return b.servers->GetServerCount(it->second.handle);
  }

  bool serverDetails(uint64_t request, int index, ServerEntry &out) override {
    Bindings &b = bindings();
    const auto it = Lists.find(request);
    if (it == Lists.end() || !b.servers || !it->second.handle)
      return false;
    const gameserveritem_t *item =
        b.servers->GetServerDetails(it->second.handle, index);
    if (!item)
      return false;
    out = ServerEntry{};
    // GetName() falls back to the connection address when the server
    // reported no name (SDK semantics); the raw m_szServerName field is
    // private in the header and must not be reached around it.
    out.name = item->GetName();
    out.map = item->m_szMap;
    out.gameDir = item->m_szGameDir;
    out.AppId = item->m_nAppID;
    out.gameDescription = item->m_szGameDescription;
    out.gameTags = item->m_szGameTags;
    out.ip = item->m_NetAdr.GetIP();
    out.queryPort = item->m_NetAdr.GetQueryPort();
    out.connectionPort = item->m_NetAdr.GetConnectionPort();
    out.pingMs = item->m_nPing;
    out.players = item->m_nPlayers;
    out.maxPlayers = item->m_nMaxPlayers;
    out.botPlayers = item->m_nBotPlayers;
    out.password = item->m_bPassword;
    out.secure = item->m_bSecure;
    out.steamId = item->m_steamID.ConvertToUint64();
    return true;
  }

  // --- single-server queries ----------------------------------------------

  uint64_t pingServer(uint32_t ip, uint16_t port) override {
    if (!ensureBound() || ip == 0)
      return 0;
    Bindings &b = bindings();
    cancelServerQuery(PingTag);
    const uint64_t tag = NextTag++;
    auto response = std::make_unique<PingResponse>(tag);
    const uint16_t queryPort = port ? port : 27015;
    const HServerQuery handle =
        b.servers->PingServer(ip, queryPort, response.get());
    if (handle == HSERVERQUERY_INVALID)
      return 0;
    ActivePing active;
    active.handle = handle;
    active.response = std::move(response);
    PingTag = tag;
    Pings.emplace(tag, std::move(active));
    return tag;
  }

  uint64_t requestPlayers(uint32_t ip, uint16_t port) override {
    if (!ensureBound() || ip == 0)
      return 0;
    Bindings &b = bindings();
    cancelServerQuery(PlayersTag);
    const uint64_t tag = NextTag++;
    auto response = std::make_unique<PlayersResponse>(tag);
    const uint16_t queryPort = port ? port : 27015;
    const HServerQuery handle =
        b.servers->PlayerDetails(ip, queryPort, response.get());
    if (handle == HSERVERQUERY_INVALID)
      return 0;
    ActivePlayers active;
    active.handle = handle;
    active.response = std::move(response);
    PlayersTag = tag;
    Players.emplace(tag, std::move(active));
    return tag;
  }

  void cancelServerQuery(uint64_t query) override {
    if (query == 0)
      return;
    Bindings &b = bindings();
    const auto ping = Pings.find(query);
    if (ping != Pings.end()) {
      if (b.servers && ping->second.handle != HSERVERQUERY_INVALID)
        b.servers->CancelServerQuery(ping->second.handle);
      // CancelServerQuery exists precisely so the response object may
      // die right after - the "confirmed silence" for single-server
      // queries.
      Pings.erase(ping);
      if (PingTag == query)
        PingTag = 0;
      return;
    }
    const auto it = Players.find(query);
    if (it == Players.end())
      return;
    if (b.servers && it->second.handle != HSERVERQUERY_INVALID)
      b.servers->CancelServerQuery(it->second.handle);
    Players.erase(it);
    if (PlayersTag == query)
      PlayersTag = 0;
  }

  // --- owner-thread drain -------------------------------------------------

  int pollEvents(QueryEvent *out, int max) override {
    if (!out || max <= 0)
      return 0;
    EventQueue &q = queue();
    std::lock_guard lock(q.mutex);
    int count = 0;
    while (count < max && !q.events.empty()) {
      out[count++] = std::move(q.events.front());
      q.events.pop_front();
    }
    return count;
  }

  int outstandingRequests() override {
    return static_cast<int>(Lists.size() + Pings.size() + Players.size());
  }

private:
  std::unordered_map<uint64_t, ActiveList> Lists;
  std::unordered_map<uint64_t, ActivePing> Pings;
  std::unordered_map<uint64_t, ActivePlayers> Players;
  uint64_t PingTag = 0;
  uint64_t PlayersTag = 0;
  uint64_t NextTag = 1;
};

// T18b: the favorites/history adapter. Steam IS the authoritative store
// (see ServerFavorites.h for the shipped-binary evidence): the same records
// the Favorites/History list queries read back. No file of ours, no second
// truth. Every operation fail-closes to false/0 when Steam is unavailable.
class SteamFavorites final : public IServerFavorites {
public:
  bool available() override {
    return ensureBindings(false) && bindings().matchmaking != nullptr;
  }

  bool isFavorite(uint32_t ip, uint16_t connPort, uint16_t queryPort) override {
    if (!available())
      return false;
    return findRecord(ip, connPort, queryPort, k_unFavoriteFlagFavorite) >= 0;
  }

  bool addFavorite(uint32_t ip, uint16_t connPort,
                   uint16_t queryPort) override {
    if (!available() || ip == 0)
      return false;
    Bindings &b = bindings();
    const int result = b.matchmaking->AddFavoriteGame(
        b.appId, ip, connPort, queryPort, k_unFavoriteFlagFavorite,
        static_cast<uint32>(nowUnix()));
    if (result < 0)
      return false;
    // Read-back: the SDK returns the index of the record it wrote; the
    // authoritative re-read is what the caller trusts (T18b step b.2
    // "收藏增删立即回读核对").
    return findRecord(ip, connPort, queryPort, k_unFavoriteFlagFavorite) >= 0;
  }

  bool removeFavorite(uint32_t ip, uint16_t connPort,
                      uint16_t queryPort) override {
    if (!available() || ip == 0)
      return false;
    Bindings &b = bindings();
    if (!b.matchmaking->RemoveFavoriteGame(b.appId, ip, connPort, queryPort,
                                           k_unFavoriteFlagFavorite))
      return false;
    return findRecord(ip, connPort, queryPort, k_unFavoriteFlagFavorite) < 0;
  }

  bool addHistory(uint32_t ip, uint16_t connPort, uint16_t queryPort) override {
    if (!available() || ip == 0)
      return false;
    Bindings &b = bindings();
    // The shipped ConnectedToGame handler's exact call (0x1000ef00
    // delegates to this shape with flags=2 and the current time).
    const int result = b.matchmaking->AddFavoriteGame(
        b.appId, ip, connPort, queryPort, k_unFavoriteFlagHistory,
        static_cast<uint32>(nowUnix()));
    return result >= 0;
  }

  int favoriteCount() override {
    if (!available())
      return 0;
    return countRecords(k_unFavoriteFlagFavorite);
  }

  int historyCount() override {
    if (!available())
      return 0;
    return countRecords(k_unFavoriteFlagHistory);
  }

private:
  static int64_t nowUnix() {
    return static_cast<int64_t>(std::time(nullptr));
  }

  int countRecords(uint32 flags) {
    Bindings &b = bindings();
    const int total = b.matchmaking->GetFavoriteGameCount();
    int count = 0;
    for (int i = 0; i < total; ++i) {
      AppId_t appid = 0;
      uint32 ip = 0;
      uint16 connPort = 0;
      uint16 queryPort = 0;
      uint32 recordFlags = 0;
      uint32 lastPlayed = 0;
      if (!b.matchmaking->GetFavoriteGame(i, &appid, &ip, &connPort, &queryPort,
                                          &recordFlags, &lastPlayed))
        continue;
      if (recordFlags & flags)
        ++count;
    }
    return count;
  }

  // The index of the first record matching the tuple and carrying `flags`;
  // -1 when absent. A zero queryPort in the probe matches the stored value
  // only when the store has none either (the shipped read-back compares the
  // tuple as stored; a stale record with queryPort 0 is matched by port
  // fallback at the call site, favoriteKeyOf).
  int findRecord(uint32_t ip, uint16_t connPort, uint16_t queryPort,
                 uint32 flags) {
    Bindings &b = bindings();
    const int total = b.matchmaking->GetFavoriteGameCount();
    for (int i = 0; i < total; ++i) {
      AppId_t appid = 0;
      uint32 recordIp = 0;
      uint16 recordConn = 0;
      uint16 recordQuery = 0;
      uint32 recordFlags = 0;
      uint32 lastPlayed = 0;
      if (!b.matchmaking->GetFavoriteGame(i, &appid, &recordIp, &recordConn,
                                          &recordQuery, &recordFlags,
                                          &lastPlayed))
        continue;
      if (!(recordFlags & flags))
        continue;
      if (recordIp != ip || recordConn != connPort)
        continue;
      if (recordQuery != queryPort)
        continue;
      return i;
    }
    return -1;
  }
};

} // namespace

IServerQueryProvider *steamQueryProvider() {
  static SteamQueryProvider instance;
  return &instance;
}

IServerFavorites *steamFavorites() {
  static SteamFavorites instance;
  return &instance;
}

} // namespace services::serverbrowser
