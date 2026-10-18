// services::serverbrowser - the production model (T17 b). Main thread only;
// the provider's thread-safe event queue is the single cross-thread boundary.
// All payload values are copied before they reach this translation unit: the
// adapter owns the gameserveritem_t -> ServerEntry conversion, the model only
// moves owned values. See ServerQuery.h for the frozen contract and the
// binary evidence behind the provider calls.

#include "ServerQuery.h"

#include <chrono>
#include <utility>

namespace services::serverbrowser {

namespace {

uint64_t steadyNowMs() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
          .count());
}

} // namespace

void ServerQuery::resetListState() {
  Entries.clear();
  RowRefreshDeadlines.clear();
  ++Revision;
  NoServersListed = false;
  Deadline = 0;
}

bool ServerQuery::begin(QueryKind kind, const FilterPair *filters,
                        int filterCount) {
  if (Closed)
    return false;
  if (filterCount < 0)
    return false;
  if (filterCount > kMaxFiltersPerQuery) {
    // The Steam provider can only forward 32 pairs; refusing is safer
    // than silently querying a different filter set.
    Status = QueryStatus::Failed;
    Failure = QueryFailure::Rejected;
    return false;
  }
  std::vector<FilterPair> NextFilters;
  if (filters && filterCount > 0)
    NextFilters.assign(filters, filters + filterCount);
  Filters = std::move(NextFilters);
  // Replace any live request: cancel the work, then hand the handle back
  // (Steam wants CancelQuery before ReleaseRequest for in-flight queries;
  // the shipped browser uses the same order at 0x10007940/0x10007ad0).
  if (Request != 0) {
    // setProvider(nullptr) is a legal teardown call; do not dereference a
    // vanished provider just to cancel the handle it used to own.
    if (Provider) {
      Provider->cancelQuery(Request);
      Provider->releaseRequest(Request);
    }
    Request = 0;
  }
  // Every attempt advances the generation, including failed attempts - the
  // UI latch keyed on it must move even when nothing was started.
  ++Generation;
  Kind = kind;
  resetListState();
  Failure = QueryFailure::None;
  FailedPings = 0;

  if (!Provider || !Provider->available()) {
    Status = QueryStatus::Failed;
    Failure = QueryFailure::Unavailable;
    return false;
  }
  const uint64_t handle =
      Provider->requestList(kind, Filters.empty() ? nullptr : Filters.data(),
                            static_cast<int>(Filters.size()));
  AppId = Provider->appId();
  if (handle == 0) {
    Status = QueryStatus::Failed;
    Failure = QueryFailure::Rejected;
    return false;
  }
  Request = handle;
  Status = QueryStatus::Querying;
  Deadline = nowMs() + TimeoutMs;
  return true;
}

void ServerQuery::pump() {
  if (!Provider)
    return;
  const uint64_t Now = nowMs();
  for (auto It = RowRefreshDeadlines.begin();
       It != RowRefreshDeadlines.end();) {
    if (Now >= It->second)
      It = RowRefreshDeadlines.erase(It);
    else
      ++It;
  }
  const int count =
      Provider->pollEvents(Events.data(), static_cast<int>(Events.size()));
  for (int i = 0; i < count; ++i) {
    if (Closed) {
      ++StaleEvents; // drain-and-discard after shutdown
      continue;
    }
    apply(Events[i]);
  }
  // Deadline enforcement: a query that never completes fails and is
  // cancelled (the handle stays for the eventual release).
  if (!Closed && Status == QueryStatus::Querying && nowMs() >= Deadline) {
    Provider->cancelQuery(Request);
    fail(QueryFailure::TimedOut);
  }
}

void ServerQuery::cancel() {
  if (Status != QueryStatus::Querying && RowRefreshDeadlines.empty())
    return;
  if (Provider && Request != 0)
    Provider->cancelQuery(Request);
  Status = QueryStatus::Cancelled; // partial snapshot stays readable
  Deadline = 0;
  RowRefreshDeadlines.clear();
}

void ServerQuery::release() {
  if (Request == 0)
    return;
  // Release implies cancel for a still-live query, but stays a separate
  // step in the model (cancel keeps the handle; release hands it back).
  if (Status == QueryStatus::Querying || !RowRefreshDeadlines.empty())
    cancel();
  if (Provider)
    Provider->releaseRequest(Request);
  Request = 0;
}

void ServerQuery::shutdown() {
  cancelDetail();
  release();
  // The list handle is back; latch closed so late provider events are
  // discarded instead of projected.
  Status = QueryStatus::Idle;
  Failure = QueryFailure::None;
  resetListState();
  Closed = true;
}

void ServerQuery::refreshAll() {
  if (Closed || Request == 0 || !Provider)
    return;
  if (Status == QueryStatus::Cancelled || Status == QueryStatus::Failed) {
    // Restart cancelled/failed work with a new provider tag. Reusing the
    // old handle would also authorize its already queued late responses.
    begin(Kind, Filters.empty() ? nullptr : Filters.data(),
          static_cast<int>(Filters.size()));
    return;
  }
  // RefreshQuery is valid while the request handle is alive - the shipped
  // Refresh button (serverbrowser.dll 0x10005230 -> slot 0x24) re-pings the
  // whole list even after a completed refresh, and Steam posts another
  // RefreshComplete ("The request object must be released by calling
  // ReleaseRequest"; the callback contract in isteammatchmaking.h). The
  // generation advances so a UI can reset its receive bookkeeping, and the
  // state returns to Querying until the new RefreshComplete arrives.
  if (Status != QueryStatus::Querying && Status != QueryStatus::Completed)
    return;
  ++Generation;
  RowRefreshDeadlines.clear();
  Failure = QueryFailure::None;
  Deadline = nowMs() + TimeoutMs;
  Status = QueryStatus::Querying;
  Provider->refreshQueryAll(Request);
}

bool ServerQuery::refreshServer(int index) {
  if (Closed || Request == 0 || !Provider)
    return false;
  // Per-server refresh re-pings one entry of a live list (slot 0x30 at
  // serverbrowser.dll 0x100057c0); valid in the same states as refreshAll.
  if (Status != QueryStatus::Querying && Status != QueryStatus::Completed)
    return false;
  if (index < 0 || index >= static_cast<int>(Entries.size()))
    return false;
  RowRefreshDeadlines[index] = nowMs() + TimeoutMs;
  Provider->refreshServer(Request, index);
  return true;
}

uint64_t ServerQuery::requestPing(const ServerEntry &server) {
  // Ping is the last-server refresh path (the shipped CDialogGameInfo
  // ping loop 0x1000ad70). Same one-slot replacement discipline as
  // PlayerDetails - CancelServerQuery first, then a fresh handle.
  if (Closed || !Provider || !Provider->available() || server.ip == 0)
    return 0;
  cancelDetail();
  Detail = DetailKind::Ping;
  DetailStatus = QueryStatus::Querying;
  DetailFailed = false;
  PingValid = false;
  const uint16_t port =
      server.queryPort ? server.queryPort : server.connectionPort;
  DetailHandle = Provider->pingServer(server.ip, port);
  if (DetailHandle == 0) {
    DetailStatus = QueryStatus::Failed;
    DetailFailed = true;
    return 0;
  }
  return DetailHandle;
}

uint64_t ServerQuery::requestPlayers(const ServerEntry &server) {
  if (Closed || !Provider || !Provider->available() || server.ip == 0)
    return 0;
  cancelDetail();
  Detail = DetailKind::Players;
  DetailStatus = QueryStatus::Querying;
  DetailFailed = false;
  Players.clear();
  const uint16_t port =
      server.queryPort ? server.queryPort : server.connectionPort;
  DetailHandle = Provider->requestPlayers(server.ip, port);
  if (DetailHandle == 0) {
    DetailStatus = QueryStatus::Failed;
    DetailFailed = true;
    return 0;
  }
  return DetailHandle;
}

void ServerQuery::cancelDetail() {
  if (DetailHandle != 0 && Provider)
    Provider->cancelServerQuery(DetailHandle);
  DetailHandle = 0;
  if (DetailStatus == QueryStatus::Querying)
    DetailStatus = QueryStatus::Cancelled;
  if (Detail != DetailKind::None && DetailStatus != QueryStatus::Querying)
    Detail = DetailKind::None;
}

void ServerQuery::apply(const QueryEvent &event) {
  switch (event.type) {
  case QueryEvent::Type::PingResponded:
  case QueryEvent::Type::PingFailedToRespond:
  case QueryEvent::Type::Players:
  case QueryEvent::Type::PlayersComplete:
    applyDetail(event);
    return;
  default:
    applyList(event);
    return;
  }
}

void ServerQuery::applyList(const QueryEvent &event) {
  // Stale = another request's handle (an old generation that must never
  // overwrite the live one) or a state that moved past querying.
  const bool RowResponse =
      event.type == QueryEvent::Type::ServerResponded ||
      event.type == QueryEvent::Type::ServerFailedToRespond;
  const bool AuthorizedRow = Status == QueryStatus::Completed && RowResponse &&
                             RowRefreshDeadlines.count(event.index) != 0;
  if (event.request != Request || Request == 0 ||
      (Status != QueryStatus::Querying && !AuthorizedRow)) {
    ++StaleEvents;
    return;
  }
  if (RowResponse)
    RowRefreshDeadlines.erase(event.index);
  switch (event.type) {
  case QueryEvent::Type::ServerResponded: {
    if (event.index < 0 || event.index >= kMaxServers) {
      ++DroppedServers;
      return;
    }
    ensureSlot(event.index);
    ServerEntry details;
    if (Provider->serverDetails(Request, event.index, details)) {
      details.responded = true;
      Entries[event.index] = std::move(details);
      ++Revision;
    }
    // Details unavailable: the slot stays a placeholder; a later
    // duplicate response (or the final fill) can still complete it.
    return;
  }
  case QueryEvent::Type::ServerFailedToRespond: {
    ++FailedPings;
    if (event.index >= 0 && event.index < kMaxServers) {
      ensureSlot(event.index);
      ServerEntry Details;
      if (Provider->serverDetails(Request, event.index, Details)) {
        Details.responded = false;
        Entries[event.index] = std::move(Details);
        ++Revision;
      }
    }
    return;
  }
  case QueryEvent::Type::RefreshComplete:
    finishQuery(event.response);
    return;
  default:
    return;
  }
}

void ServerQuery::finishQuery(int response) {
  switch (response) {
  case 0: // eServerResponded
    fillMissingDetails();
    Status = QueryStatus::Completed;
    break;
  case 2: // eNoServersListedOnMasterServer
    fillMissingDetails();
    NoServersListed = true;
    Status = QueryStatus::Completed;
    break;
  default: // eServerFailedToRespond
    fail(QueryFailure::MasterNoResponse);
    break;
  }
  Deadline = 0;
}

// After RefreshComplete the list is final: indices that never sent a
// ServerResponded still have (ping-less) details - the shipped browser reads
// them the same way when it builds rows for silent servers.
void ServerQuery::fillMissingDetails() {
  int total = Provider->serverCount(Request);
  if (total < 0)
    total = 0;
  if (total > kMaxServers) {
    DroppedServers += total - kMaxServers;
    total = kMaxServers;
  }
  if (total > static_cast<int>(Entries.size()))
    Entries.resize(total);
  for (int i = 0; i < total; ++i) {
    if (Entries[i].responded)
      continue;
    ServerEntry details;
    if (Provider->serverDetails(Request, i, details))
      Entries[i] = std::move(details);
  }
  ++Revision;
}

void ServerQuery::ensureSlot(int index) {
  if (index >= static_cast<int>(Entries.size())) {
    Entries.resize(index + 1);
    ++Revision;
  }
}

void ServerQuery::fail(QueryFailure failure) {
  Status = QueryStatus::Failed;
  Failure = failure;
  Deadline = 0;
}

uint64_t ServerQuery::nowMs() const {
  return Clock ? Clock() : steadyNowMs();
}

// --- single-server detail queries (PingServer / PlayerDetails) --------------

void ServerQuery::applyDetail(const QueryEvent &event) {
  if (event.request != DetailHandle || DetailHandle == 0 ||
      DetailStatus != QueryStatus::Querying) {
    ++StaleEvents;
    return;
  }
  switch (event.type) {
  case QueryEvent::Type::PingResponded:
    if (Detail == DetailKind::Ping) {
      Ping = event.server; // owned copy: the event owns its strings
      PingValid = true;
    }
    DetailStatus = QueryStatus::Completed;
    return;
  case QueryEvent::Type::PingFailedToRespond:
    DetailFailed = true;
    DetailStatus = QueryStatus::Failed;
    return;
  case QueryEvent::Type::Players:
    for (const PlayerLine &line : event.players) {
      if (static_cast<int>(Players.size()) >= kMaxPlayers)
        break;
      Players.push_back(line); // quoted copy: the event owns its strings
    }
    return;
  case QueryEvent::Type::PlayersComplete:
    DetailFailed = event.failed;
    DetailStatus = event.failed ? QueryStatus::Failed : QueryStatus::Completed;
    return;
  default:
    return;
  }
}

} // namespace services::serverbrowser
