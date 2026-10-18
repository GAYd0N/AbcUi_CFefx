// services::friends::FriendsModel implementation - see FriendsModel.h.
#include "FriendsModel.h"

#include <algorithm>

namespace services::friends {

namespace {

// A row is "interesting" when the friend is in a game; joinable is stronger.
bool better(const FriendEntry &left, const FriendEntry &right) {
  const int leftRank = left.Connect.empty() ? (left.InGame ? 1 : 2) : 0;
  const int rightRank = right.Connect.empty() ? (right.InGame ? 1 : 2) : 0;
  if (leftRank != rightRank)
    return leftRank < rightRank;
  return left.Name < right.Name;
}

} // namespace

void FriendsModel::setProvider(IFriendsProvider *provider) {
  if (Provider == provider)
    return;
  Provider = provider;
  Refreshed = false;
  SinceRefresh = 0.0;
}

bool FriendsModel::available() const {
  return Provider && Provider->available();
}

void FriendsModel::refresh(double frameTime, uint64_t localSteamId,
                           const char *localName) {
  SinceRefresh += frameTime > 0.0 ? frameTime : 0.0;
  if (Refreshed && SinceRefresh < kRefreshSeconds)
    return;
  SinceRefresh = 0.0;
  Refreshed = true;
  rebuild(localSteamId, localName);
}

void FriendsModel::rebuild(uint64_t localSteamId, const char *localName) {
  std::vector<FriendEntry> next;
  LastCount = 0;
  if (available()) {
    const int total = Provider->count();
    LastCount = total;
    for (int index = 0;
         index < total && static_cast<int>(next.size()) < kMaxRows; ++index) {
      FriendEntry entry;
      if (!Provider->entry(index, entry))
        continue;
      if (entry.Name.empty())
        continue;
      // The board is "who can I play with right now": friends who are
      // not in a game are not rows (the requirement's Alien Swarm shape).
      if (!entry.InGame)
        continue;
      next.push_back(std::move(entry));
    }
  }
  std::sort(next.begin(), next.end(), better);
  if (static_cast<int>(next.size()) > kMaxRows)
    next.resize(kMaxRows);

  // The local player is always the first row, even with no Steam presence.
  FriendEntry self;
  self.SteamId = localSteamId;
  self.Name = localName && *localName ? localName : "Player";
  self.InGame = false;
  std::vector<FriendEntry> rows;
  rows.push_back(std::move(self));
  rows.insert(rows.end(), next.begin(), next.end());

  const bool changed =
      rows.size() != Rows.size() ||
      !std::equal(rows.begin(), rows.end(), Rows.begin(),
                  [](const FriendEntry &left, const FriendEntry &right) {
                    return left.SteamId == right.SteamId &&
                           left.Name == right.Name && left.Game == right.Game &&
                           left.Connect == right.Connect &&
                           left.OtherGame == right.OtherGame;
                  });
  if (changed) {
    Rows = std::move(rows);
    ++Generation;
  }
}

FriendsModel &friendsModel() {
  static FriendsModel Instance;
  return Instance;
}

} // namespace services::friends
