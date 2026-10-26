#pragma once

// services::serverbrowser - the T17 server data provider: an asynchronous
// list/ping/details query lifecycle on top of a provider seam. This header is
// pure (no engine or Steam includes) so the exact production model links into
// the offline acceptance test (05 doc tier 1); the Steam adapter
// (ServerQuerySteam.cpp) implements IServerQueryProvider.
//
// ServerQuerySteam binds ISteamMatchmakingServers directly from the host's
// steam_api.dll using the SDK interface version and RuntimeAccess. The engine
// owns Steam initialization, shutdown and callback pumping.
//
// Lifecycle contract (T17 step b, frozen for T18):
//   - States: Idle / Querying / Completed / Cancelled / Failed. Failure
//     values: Unavailable / Rejected / TimedOut / MasterNoResponse.
//   - Every begin() and every refreshAll() increments the generation. The
//     provider request handle tags every delivery; an event whose handle does
//     not match the live request (or that arrives after cancel/completion) is
//     dropped as stale and can never overwrite newer results.
//   - Cancellation and release are modeled separately: cancel() stops the
//     query and the projection (handle kept), release() hands the handle back
//     (cancel first when still live). Page leave uses release(); shutdown()
//     releases everything and latches the model closed.
//   - Callbacks never touch the DOM and never borrow pointers: the adapter
//     copies all payload data into owned QueryEvent values on the callback
//     thread; this model drains them on the owner thread in pump().
//   - Bounded everywhere: kMaxServers snapshot cap, kMaxPlayers lines,
//     kMaxEventsPerPump per frame, provider-side queue cap, deadline timeout.

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace services::serverbrowser {

// Frozen bounds (T17 step b: 超时、数量上限).
inline constexpr int kMaxServers = 1000; // list snapshot cap
inline constexpr int kMaxPlayers = 64;   // per-server player lines
inline constexpr int kMaxEventsPerPump =
    256; // bounded main-thread work per frame
inline constexpr int kMaxFiltersPerQuery =
    32; // Steam MatchMakingKeyValuePair_t limit
inline constexpr uint32_t kDefaultTimeoutMs = 30000;

// The six Steam list kinds (slots 0x00-0x14; T18 exposes Internet/LAN/
// Favorites/History; Spectate/Friends ride the same contract).
enum class QueryKind : uint8_t {
  Internet,
  Lan,
  Favorites,
  History,
  Spectate,
  Friends
};

enum class QueryStatus : uint8_t {
  Idle,
  Querying,
  Completed,
  Cancelled,
  Failed
};

enum class QueryFailure : uint8_t {
  None,
  Unavailable,      // no live Steam / interface - fail closed
  Rejected,         // provider refused the request (null handle)
  TimedOut,         // no RefreshComplete within the deadline
  MasterNoResponse, // RefreshComplete(eServerFailedToRespond)
};

// The two single-server query kinds (PingServer / PlayerDetails paths).
enum class DetailKind : uint8_t { None, Ping, Players };

struct FilterPair {
  std::string key;
  std::string value;
};

// One server snapshot row. All strings are owned copies of the
// gameserveritem_t fields (copied on the owner thread inside
// serverDetails()); nothing borrows from Steam storage.
struct ServerEntry {
  std::string name;
  std::string map;
  std::string gameDir;
  std::string gameDescription;
  std::string gameTags;
  uint32_t ip = 0;
  uint16_t queryPort = 0;
  uint16_t connectionPort = 0;
  int pingMs = 0;
  int players = 0;
  int maxPlayers = 0;
  int botPlayers = 0;
  bool password = false;
  bool secure = false;
  uint64_t steamId = 0;
  bool responded = false; // a ServerResponded was seen for this index
  uint32_t AppId = 0;
};

struct PlayerLine {
  std::string name;
  int score = 0;
  float seconds = 0.0f;
};

// One provider -> model delivery. `request` is the opaque handle the provider
// returned (Steam HServerListRequest / HServerQuery); the model drops any
// event whose handle is not the live one, or whose state moved on.
struct QueryEvent {
  enum class Type : uint8_t {
    ServerResponded,
    ServerFailedToRespond,
    RefreshComplete,
    PingResponded,
    PingFailedToRespond,
    Players,
    PlayersComplete,
  };
  Type type = Type::ServerResponded;
  uint64_t request = 0;
  int index = 0;       // ServerResponded / ServerFailedToRespond
  int response = 0;    // RefreshComplete: EMatchMakingServerResponse
  bool failed = false; // PingFailedToRespond / PlayersComplete(failed)
  std::vector<PlayerLine> players; // Players (owned copies)
  ServerEntry server;              // PingResponded payload (owned copies)
};

// Provider seam. Production: ServerQuerySteam (steam_api.dll via the proven
// binding). Offline: a scripted fake. request*/cancel*/release* calls happen
// on the owner (main) thread; callback deliveries are queued inside the
// provider (thread-safe) and drained by the model through pollEvents().
class IServerQueryProvider {
public:
  virtual ~IServerQueryProvider() = default;

  virtual bool available() = 0; // steam up + interface bound (may re-probe)
  virtual uint32_t appId() const {
    return 0;
  }

  virtual uint64_t requestList(QueryKind kind, const FilterPair *filters,
                               int filterCount) = 0;
  virtual void
  cancelQuery(uint64_t request) = 0; // CancelQuery: stop work, keep handle
  virtual void
  releaseRequest(uint64_t request) = 0; // ReleaseRequest: hand the handle back

  virtual void refreshQueryAll(uint64_t request) = 0;          // RefreshQuery
  virtual void refreshServer(uint64_t request, int index) = 0; // RefreshServer

  virtual int serverCount(uint64_t request) = 0;
  virtual bool serverDetails(uint64_t request, int index, ServerEntry &out) = 0;

  virtual uint64_t pingServer(uint32_t ip, uint16_t port) = 0; // PingServer
  virtual uint64_t requestPlayers(uint32_t ip,
                                  uint16_t port) = 0; // PlayerDetails
  virtual void cancelServerQuery(uint64_t query) = 0; // CancelServerQuery

  // Owner-thread drain of the provider's bounded event queue.
  virtual int pollEvents(QueryEvent *out, int max) = 0;
  // Diagnostics: outstanding request/query handles (0 = no leaks).
  virtual int outstandingRequests() = 0;
};

// The production model. Main thread only (the module drives it from onFrame /
// dispatch); the provider's queue is the only cross-thread boundary.
class ServerQuery {
public:
  void setProvider(IServerQueryProvider *provider) {
    Provider = provider;
  }
  using ClockFn = uint64_t (*)();
  void setClock(ClockFn clock) {
    Clock = clock;
  }
  void setTimeoutMs(uint32_t ms) {
    TimeoutMs = ms ? ms : kDefaultTimeoutMs;
  }

  // Start a list request of `kind`; `filters` are copied by the provider.
  // Replaces (cancel + release) any live request. False = no request is
  // live (Unavailable/Rejected/closed); the status reflects why.
  bool begin(QueryKind kind, const FilterPair *filters = nullptr,
             int filterCount = 0);

  // Owner-thread beat: drains provider events (bounded), applies them, and
  // enforces the deadline. Safe (drain-and-discard) after shutdown.
  void pump();

  // Stop the live query and the projection; the handle stays allocated.
  void cancel();
  // Cancel (when still live) then hand the handle back. Page-leave path.
  void release();
  // Release everything and latch closed; late events are discarded.
  void shutdown();

  void refreshAll();
  bool refreshServer(int index);

  // Separate single-server queries (PingServer / PlayerDetails, one slot
  // each; a new request of either kind replaces the previous one and
  // CancelServerQuery returns the old handle).
  uint64_t requestPing(const ServerEntry &server);
  uint64_t requestPlayers(const ServerEntry &server);
  void cancelDetail();

  // --- list status (read by the module / Debug diagnostics) --------------
  QueryStatus status() const {
    return Status;
  }
  QueryFailure failure() const {
    return Failure;
  }
  QueryKind kind() const {
    return Kind;
  }
  uint32_t appId() const {
    return AppId;
  }
  uint32_t generation() const {
    return Generation;
  }
  uint64_t revision() const {
    return Revision;
  }
  int count() const {
    return static_cast<int>(Entries.size());
  }
  bool noServersListed() const {
    return NoServersListed;
  }
  const ServerEntry *entry(int index) const {
    return index >= 0 && index < static_cast<int>(Entries.size())
               ? &Entries[index]
               : nullptr;
  }

  // --- detail status -----------------------------------------------------
  DetailKind detailKind() const {
    return Detail;
  }
  QueryStatus detailStatus() const {
    return DetailStatus;
  }
  bool detailFailed() const {
    return DetailFailed;
  }
  int playerCount() const {
    return static_cast<int>(Players.size());
  }
  const PlayerLine *player(int index) const {
    return index >= 0 && index < static_cast<int>(Players.size())
               ? &Players[index]
               : nullptr;
  }
  const ServerEntry *pingResult() const {
    return Detail == DetailKind::Ping && PingValid ? &Ping : nullptr;
  }
  uint64_t detailRequest() const {
    return DetailHandle;
  }

  // Diagnostics counters (live-acceptance observability).
  uint64_t staleEvents() const {
    return StaleEvents;
  }
  int droppedServers() const {
    return DroppedServers;
  }
  int failedPings() const {
    return FailedPings;
  }
  uint64_t listRequest() const {
    return Request;
  }
  bool closed() const {
    return Closed;
  }
  // Handles the provider still holds (0 = nothing dangling; the acceptance
  // reads this after release/shutdown as the "resources back to baseline"
  // fact).
  int providerOutstanding() const {
    return Provider ? Provider->outstandingRequests() : 0;
  }

private:
  void apply(const QueryEvent &event);
  void applyList(const QueryEvent &event);
  void applyDetail(const QueryEvent &event);
  void finishQuery(int response);
  void fillMissingDetails();
  void resetListState();
  void fail(QueryFailure failure);
  void ensureSlot(int index);
  uint64_t nowMs() const;

  IServerQueryProvider *Provider = nullptr;
  ClockFn Clock = nullptr;
  uint32_t TimeoutMs = kDefaultTimeoutMs;

  QueryKind Kind = QueryKind::Internet;
  uint32_t AppId = 0;
  QueryStatus Status = QueryStatus::Idle;
  QueryFailure Failure = QueryFailure::None;
  uint32_t Generation = 0;
  uint64_t Revision = 0;
  std::vector<FilterPair> Filters;
  // Completed-list refreshes authorize only the explicitly requested rows.
  std::map<int, uint64_t> RowRefreshDeadlines;
  uint64_t Request = 0;
  uint64_t Deadline = 0;
  bool NoServersListed = false;
  bool Closed = false;
  std::vector<ServerEntry> Entries;
  // Reused drain buffer: 256 QueryEvent values (~50 KB) no longer live on
  // the per-frame stack.
  std::array<QueryEvent, kMaxEventsPerPump> Events;

  DetailKind Detail = DetailKind::None;
  QueryStatus DetailStatus = QueryStatus::Idle;
  bool DetailFailed = false;
  uint64_t DetailHandle = 0;
  std::vector<PlayerLine> Players;
  ServerEntry Ping;
  bool PingValid = false;

  uint64_t StaleEvents = 0;
  int DroppedServers = 0;
  int FailedPings = 0;
};

// The production provider (ServerQuerySteam.cpp). Never null; available()
// reports whether the live Steam client currently serves the binding. The
// provider is a process-lifetime singleton like the other service backends.
IServerQueryProvider *steamQueryProvider();

} // namespace services::serverbrowser
