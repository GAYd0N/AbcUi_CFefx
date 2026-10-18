// services::serverbrowser::ServerListModel implementation - see the header
// for the shipped-binary evidence and the frozen view contract.
#include "ServerListModel.h"

#include <algorithm>
#include <cctype>

namespace services::serverbrowser {

namespace {

bool iequals(const std::string &a, const std::string &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

// Case-insensitive name/map comparator (the shipped list sorts names
// case-insensitively; CGameListPanel default). Three-way form so one
// comparator drives both directions.
int textCompare(const std::string &a, const std::string &b) {
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    const int ca = std::tolower(static_cast<unsigned char>(a[i]));
    const int cb = std::tolower(static_cast<unsigned char>(b[i]));
    if (ca != cb)
      return ca < cb ? -1 : 1;
  }
  if (a.size() == b.size())
    return 0;
  return a.size() < b.size() ? -1 : 1;
}

} // namespace

int ServerFilter::buildPairs(FilterPair *out, int max) const {
  if (!out || max <= 0)
    return 0;
  int count = 0;
  const auto add = [&out, &count, max](const char *key, const char *value) {
    if (count < max) {
      out[count].key = key;
      out[count].value = value;
      ++count;
    }
  };
  // Shipped order (0x10007f50): gamedir, empty, full, secure.
  if (!gameDir.empty())
    add("gamedir", gameDir.c_str());
  if (hideEmpty)
    add("empty", "1");
  if (hideFull)
    add("full", "1");
  if (secureOnly)
    add("secure", "1");
  return count;
}

bool ServerFilter::accepts(const ServerEntry &entry) const {
  if (!entry.responded)
    return !HideUnresponsive;
  auto Contains = [](std::string Text, std::string Needle) {
    auto Lower = [](unsigned char C) {
      return static_cast<char>(std::tolower(C));
    };
    std::transform(Text.begin(), Text.end(), Text.begin(), Lower);
    std::transform(Needle.begin(), Needle.end(), Needle.begin(), Lower);
    return Text.find(Needle) != std::string::npos;
  };
  if (!Contains(entry.name, Name) || !Contains(entry.map, Map))
    return false;
  if (NoPassword && entry.password)
    return false;
  if (InsecureOnly && entry.secure)
    return false;
  if (MaxPing > 0 && (entry.pingMs < 0 || entry.pingMs > MaxPing))
    return false;
  if (hideEmpty && entry.players <= 0)
    return false;
  if (hideFull && entry.maxPlayers > 0 && entry.players >= entry.maxPlayers)
    return false;
  if (secureOnly && !entry.secure)
    return false;
  if (!gameDir.empty() && !entry.gameDir.empty() &&
      !iequals(gameDir, entry.gameDir))
    return false;
  return true;
}

uint64_t ServerListModel::keyOf(const ServerEntry &entry) {
  return (static_cast<uint64_t>(entry.ip) << 16) | entry.queryPort;
}

void ServerListModel::setFilter(const ServerFilter &filter) {
  Filter = filter;
}

void ServerListModel::setSort(ServerSort column, bool ascending) {
  SortColumn = column;
  SortAscending = ascending;
}

bool ServerListModel::sync(const ServerQuery &query) {
  const uint32_t generation = query.generation();
  const int count = query.count();
  const bool filterMoved = !(Filter == LastFilter);
  const bool sortMoved =
      SortColumn != LastSortColumn || SortAscending != LastSortAscending;
  if (HasSnapshot && generation == LastGeneration &&
      query.revision() == LastRevision && count == LastCount && !filterMoved &&
      (!Blacklist || Blacklist->revision() == LastBlacklistRevision) &&
      !sortMoved)
    return false; // same snapshot, same view: nothing to do
  LastGeneration = generation;
  LastRevision = query.revision();
  LastCount = count;
  LastFilter = Filter;
  LastSortColumn = SortColumn;
  LastSortAscending = SortAscending;
  HasSnapshot = true;
  LastBlacklistRevision = Blacklist ? Blacklist->revision() : 0;

  // A new generation replaces every row: selection survives only by key.
  rebuild(query);
  // Selection of a vanished server is gone (the original list clears it).
  if (SelectedKey != 0 && selectedRow() < 0) {
    bool present = false;
    for (const Row &row : Rows) {
      if (row.key == SelectedKey) {
        present = true;
        break;
      }
    }
    if (!present)
      SelectedKey = 0;
  }
  clampPage();
  return true;
}

void ServerListModel::rebuild(const ServerQuery &query) {
  Rows.clear();
  const int count = query.count();
  Rows.reserve(count);
  for (int i = 0; i < count; ++i) {
    const ServerEntry *entry = query.entry(i);
    if (!entry)
      continue;
    // Compare Steam's reported AppID with the running application's
    // ISteamUtils::GetAppID(), not a spoofable display description.
    if ((entry->responded || entry->AppId != 0) &&
        entry->AppId != query.appId())
      continue;
    const bool Blocked =
        Blacklist && Blacklist->contains(entry->ip, entry->connectionPort
                                                        ? entry->connectionPort
                                                        : entry->queryPort);
    if (Blocked != BlockedOnly || (!BlockedOnly && !Filter.accepts(*entry)))
      continue;
    Row row;
    row.key = keyOf(*entry);
    row.index = i;
    Rows.push_back(row);
  }
  const ServerQuery *q = &query;
  std::stable_sort(
      Rows.begin(), Rows.end(), [q, this](const Row &a, const Row &b) {
        const ServerEntry *ea = q->entry(a.index);
        const ServerEntry *eb = q->entry(b.index);
        if (!ea || !eb)
          return false;
        int cmp = 0;
        switch (SortColumn) {
        case ServerSort::Name:
          cmp = textCompare(ea->name, eb->name);
          break;
        case ServerSort::Map:
          cmp = textCompare(ea->map, eb->map);
          break;
        case ServerSort::Players:
          cmp = (ea->players > eb->players) - (ea->players < eb->players);
          break;
        case ServerSort::Ping:
          // Unknown ping (never responded) is the LAST group in BOTH
          // directions - it is not a fast ping, it is no ping at all.
          if (ea->responded != eb->responded)
            return ea->responded; // a known, b unknown -> a first
          if (!ea->responded)
            return false; // both unknown: keep snapshot order (stable)
          cmp = (ea->pingMs > eb->pingMs) - (ea->pingMs < eb->pingMs);
          break;
        }
        return SortAscending ? cmp < 0 : cmp > 0;
      });
}

void ServerListModel::clampPage() {
  const int pages = pageCount();
  if (Page >= pages)
    Page = pages - 1;
  if (Page < 0)
    Page = 0;
}

int ServerListModel::pageCount() const {
  if (Rows.empty())
    return 1;
  return static_cast<int>((Rows.size() + kPageSize - 1) / kPageSize);
}

int ServerListModel::entryIndexAt(int row) const {
  // Rows outside the current page window are not view rows at all - the
  // render cap is part of the contract, not the caller's business.
  if (row < 0 || row >= kPageSize)
    return -1;
  const int offset = Page * kPageSize + row;
  if (offset >= static_cast<int>(Rows.size()))
    return -1;
  return Rows[offset].index;
}

uint64_t ServerListModel::keyAt(int row) const {
  if (row < 0 || row >= kPageSize)
    return 0;
  const int offset = Page * kPageSize + row;
  if (offset >= static_cast<int>(Rows.size()))
    return 0;
  return Rows[offset].key;
}

bool ServerListModel::setPage(int page) {
  if (page < 0 || page >= pageCount() || page == Page)
    return false;
  Page = page;
  return true;
}

bool ServerListModel::pagePrev() {
  return setPage(Page - 1);
}

bool ServerListModel::pageNext() {
  return setPage(Page + 1);
}

void ServerListModel::selectRow(int row) {
  const uint64_t key = keyAt(row);
  if (key != 0)
    SelectedKey = key;
}

int ServerListModel::selectedRow() const {
  if (SelectedKey == 0)
    return -1;
  const int begin = Page * kPageSize;
  const int end =
      std::min<int>(begin + kPageSize, static_cast<int>(Rows.size()));
  for (int offset = begin; offset < end; ++offset) {
    if (Rows[offset].key == SelectedKey)
      return offset - begin;
  }
  return -1;
}

const ServerEntry *
ServerListModel::selectedEntry(const ServerQuery &query) const {
  if (SelectedKey == 0)
    return nullptr;
  for (const Row &row : Rows) {
    if (row.key == SelectedKey)
      return query.entry(row.index);
  }
  return nullptr;
}

} // namespace services::serverbrowser
