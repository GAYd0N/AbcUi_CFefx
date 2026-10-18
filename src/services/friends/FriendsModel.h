#pragma once

// services::friends::FriendsModel - the main menu's friends board.
//
// Engine-free (the Steam adapter is an injected provider seam, mirroring the
// server browser's split): the model owns the policy - how often the list is
// re-read, which friends are shown, the row order (the local player first,
// then friends who are in a joinable game, then the rest by name) and the
// joinable address, normalized to "ip:port".
//
// The Steam side deliberately reads only what the join feature needs: the
// persona name, whether the friend is in a game, that game's name and its
// address (rich presence "connect" first - the Source-family convention the
// Sven Co-op client also publishes - then the game-info IP/port).

#include <cstdint>
#include <string>
#include <vector>

namespace services::friends {

struct FriendEntry {
  uint64_t SteamId = 0;
  std::string Name;
  std::string Game;    // e.g. "Sven Co-op"; empty when not in a game
  std::string Connect; // "ip:port" when joinable, empty when not
  bool InGame = false;
  // The friend plays a game that is not this one. The board names that
  // fact instead of printing the other game's AppID (a friend in our own
  // game keeps Game, which spells our name).
  bool OtherGame = false;
};

// Provider seam: one enumeration of the friend list.
class IFriendsProvider {
public:
  virtual ~IFriendsProvider() = default;
  // Steam reachable AND the friends interface resolved.
  virtual bool available() = 0;
  // Number of friends to enumerate (capped by the adapter).
  virtual int count() = 0;
  // Fills one entry; false for an unreadable index.
  virtual bool entry(int index, FriendEntry &out) = 0;
};

// Process-scoped Steam adapter (binds steam_api.dll's friends interface).
IFriendsProvider *steamFriendsProvider();

class FriendsModel {
public:
  // The board is a menu surface, not a roster: a bounded, sorted top list.
  static constexpr int kMaxRows = 32;
  static constexpr double kRefreshSeconds = 15.0;

  void setProvider(IFriendsProvider *provider);
  bool available() const;
  // Forces the next refresh() to re-enumerate (provider swap, debugging).
  void invalidate() {
    Refreshed = false;
    SinceRefresh = 0.0;
  }

  // Owner beat. Re-reads the list at most every kRefreshSeconds (and the
  // first time immediately), sorts it, and bumps generation() when the row
  // set changed - the page then re-projects instead of writing every frame.
  void refresh(double frameTime, uint64_t localSteamId, const char *localName);

  // Rows in display order: the local player first, then joinable friends,
  // then the remaining in-game friends, then everyone else (by name).
  const std::vector<FriendEntry> &rows() const {
    return Rows;
  }
  uint32_t generation() const {
    return Generation;
  }
  // Diagnostics for the probe: the last enumeration's raw friend count.
  int lastFriendCount() const {
    return LastCount;
  }

private:
  void rebuild(uint64_t localSteamId, const char *localName);

  IFriendsProvider *Provider = nullptr;
  std::vector<FriendEntry> Rows;
  uint32_t Generation = 0;
  double SinceRefresh = 0.0;
  bool Refreshed = false;
  int LastCount = 0;
};

FriendsModel &friendsModel();

} // namespace services::friends
