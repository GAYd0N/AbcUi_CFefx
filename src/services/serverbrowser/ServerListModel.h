#pragma once

// services::serverbrowser::ServerListModel - the T18 view model over one
// ServerQuery snapshot: client-side filter + sort + stable row identity +
// a bounded page window. Pure (no engine, Steam or Rml includes) so the exact
// production logic links into the offline acceptance test (05 doc tier 1).
//
// Master-side filters use the Steam SDK keys gamedir, empty, full and secure.
// Rows are also checked client-side so a master ignoring filters cannot fake
// the visible list. Name/map/players/ping sort the query snapshot fields;
// password, bots and secure markers belong to the row projection.
//
// Contract pinned for the page (and the tests):
//   - Stable identity is (ip, queryPort) - keyOf(). Rows are updated in
//     place across generations; a selection made by key never jumps to a
//     different server after a sync (T18a step 2).
//   - Unresponsive entries are hidden by default. When explicitly enabled,
//     they retain unknown values and sort last for ping in either direction.
//   - Known foreign AppIDs are always rejected; blacklist rules are checked
//     against connection ports before applying the ordinary view filters.
//   - The rendered window is bounded: kPageSize rows per page, paging is
//     part of the model (page()/pageCount()/setPage()).

#include "ServerQuery.h"
#include "ServerBlacklist.h"

#include <cstdint>
#include <string>
#include <vector>

namespace services::serverbrowser {

// The master filter pairs, verbatim shipped keys and order (0x10007f50).
struct ServerFilter {
  bool hideEmpty = false;
  bool hideFull = false;
  bool secureOnly = false;
  std::string gameDir; // "svencoop"; empty sends no gamedir pair
  bool HideUnresponsive = true;
  bool NoPassword = false;
  bool InsecureOnly = false;
  int MaxPing = 0;
  std::string Name;
  std::string Map;

  // Up to 4 pairs; returns the written count.
  int buildPairs(FilterPair *out, int max) const;
  // The client-side twin of the master rules (case-insensitive gamedir).
  // Unknown entries pass value filters only when HideUnresponsive is false.
  bool accepts(const ServerEntry &entry) const;
  bool operator==(const ServerFilter &) const = default;
};

// Sort keys (the data columns of the list). Ping treats "unknown" specially.
enum class ServerSort : uint8_t { Name, Map, Players, Ping };

class ServerListModel {
public:
  // Frozen render cap (T18a: bounded list rendering; paging keeps the rest
  // reachable without an unbounded DOM).
  static constexpr int kPageSize = 64;

  // Stable server identity: (ip, queryPort). 0 is never a valid key.
  static uint64_t keyOf(const ServerEntry &entry);

  void setFilter(const ServerFilter &filter);
  const ServerFilter &filter() const {
    return Filter;
  }
  void setBlacklist(ServerBlacklist *Value) {
    Blacklist = Value;
    HasSnapshot = false;
  }
  void setBlockedOnly(bool Value) {
    BlockedOnly = Value;
    HasSnapshot = false;
  }
  bool blockedOnly() const {
    return BlockedOnly;
  }

  void setSort(ServerSort column, bool ascending);
  ServerSort sortColumn() const {
    return SortColumn;
  }
  bool sortAscending() const {
    return SortAscending;
  }

  // Rebuilds the view from the query's CURRENT snapshot. Cheap no-op while
  // neither snapshot identity/content (generation/revision/count) nor
  // filter/sort moved.
  // Returns true when the view contents changed. Selection follows its key:
  // still present -> same server selected; gone -> cleared.
  bool sync(const ServerQuery &query);

  // --- view ---------------------------------------------------------------
  int rowCount() const {
    return static_cast<int>(Rows.size());
  }
  // Snapshot index behind a view row, or -1. Resolve the entry through the
  // SAME query generation sync() ran on (the module keeps it alive).
  int entryIndexAt(int row) const;
  uint64_t keyAt(int row) const;

  // --- paging ---------------------------------------------------------------
  int page() const {
    return Page;
  }
  int pageCount() const;
  bool setPage(int page);
  bool pagePrev();
  bool pageNext();

  // --- selection (by stable key) -------------------------------------------
  void selectRow(int row); // a row of the CURRENT page
  void clearSelection() {
    SelectedKey = 0;
  }
  bool selected() const {
    return SelectedKey != 0;
  }
  uint64_t selectedKey() const {
    return SelectedKey;
  }
  // Row within the current page, or -1 (other page / none).
  int selectedRow() const;
  // Live snapshot entry of the selection, or null (gone / no selection).
  const ServerEntry *selectedEntry(const ServerQuery &query) const;

private:
  struct Row {
    uint64_t key = 0;
    int index = 0; // snapshot index at sync time
  };

  void rebuild(const ServerQuery &query);
  void clampPage();

  ServerFilter Filter;
  ServerBlacklist *Blacklist = nullptr;
  bool BlockedOnly = false;
  uint64_t LastBlacklistRevision = 0;
  ServerSort SortColumn = ServerSort::Name;
  bool SortAscending = true;

  std::vector<Row> Rows;
  int Page = 0;
  uint64_t SelectedKey = 0;

  // Sync gates (cheap "nothing moved" exit).
  uint32_t LastGeneration = 0;
  uint64_t LastRevision = 0;
  int LastCount = -1;
  ServerFilter LastFilter;
  ServerSort LastSortColumn = ServerSort::Name;
  bool LastSortAscending = true;
  bool HasSnapshot = false;
};

} // namespace services::serverbrowser
