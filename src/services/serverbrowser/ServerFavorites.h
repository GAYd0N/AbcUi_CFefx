#pragma once

// services::serverbrowser::favorites - the T18b favorites/history seam.
//
// The authoritative store is STEAM, not a file of ours (T18 step b.2:
// "若 a 发现宿主已有权威存储，使用该存储，不建双真值"). Evidence (installed
// serverbrowser.dll, Ghidra; image base 0x10000000):
//   - steam_api.dll already exposes ISteamMatchmaking "SteamMatchMaking009";
//     the shipped browser binds it through the same modern lazy singleton the
//     T17 adapter uses (SteamInternal_ContextInit at 0x10085024, binding stubs
//     0x1000c090/0x1000c030/0x1000bce0).
//   - vtable slots verified at shipped call sites:
//       +0x00 GetFavoriteGameCount  (0x1000bce0 - empty-favorites check)
//       +0x04 GetFavoriteGame       (the list fill)
//       +0x08 AddFavoriteGame       (0x1000e710: appid, ip, connPort,
//                                    queryPort, flags=1, time)
//       +0x0c RemoveFavoriteGame    (0x1000bf70: ip, connPort, queryPort,
//                                    flags=1)
//   - flags are the SDK's k_unFavoriteFlagFavorite (1) /
//   k_unFavoriteFlagHistory
//     (2); the shipped ConnectedToGame handler (0x1000ef00) records every
//     successful join with flags=2 - history is Steam's own "最近玩过的
//    服务器" list, and RequestFavoritesServerList / RequestHistoryServerList
//     (T17-proven slots 0x0c/0x10) read those same records back. One truth,
//     persisted by Steam itself across restarts (no schema of ours).
//
// This header is pure (no engine or Steam includes) so the connect-address
// grammar and the favorite-state decision table link into the offline test;
// the Steam adapter (ServerQuerySteam.cpp) implements IServerFavorites.

#include "ServerQuery.h"

#include <cstdint>
#include <string>

namespace services::serverbrowser {

// The authoritative favorites/history operations. All calls are
// owner-thread; every one fail-closes (returns false / 0) when the Steam
// binding is unavailable.
class IServerFavorites {
public:
  virtual ~IServerFavorites() = default;

  virtual bool available() = 0;
  // Steam records key on (ip, connPort, queryPort) + flags; queryPort may
  // be 0 for a stale record (the SDK stores what it was told).
  virtual bool isFavorite(uint32_t ip, uint16_t connPort,
                          uint16_t queryPort) = 0;
  virtual bool addFavorite(uint32_t ip, uint16_t connPort,
                           uint16_t queryPort) = 0;
  virtual bool removeFavorite(uint32_t ip, uint16_t connPort,
                              uint16_t queryPort) = 0;
  // The connect-memory entry (flags=2): the shipped browser writes it when
  // a join succeeds; it is what the History tab lists.
  virtual bool addHistory(uint32_t ip, uint16_t connPort,
                          uint16_t queryPort) = 0;
  // Live counts (diagnostics / empty-state truth).
  virtual int favoriteCount() = 0;
  virtual int historyCount() = 0;
};

// The production adapter (ServerQuerySteam.cpp): bounded to the proven
// vtable slots above. Never null; available() reports whether the live
// Steam client currently serves the binding (auto re-probe, like the query
// provider).
IServerFavorites *steamFavorites();

// --- pure helpers (offline-tested) ------------------------------------------

// Dotted-quad text of a Steam little-endian ip value (gameserveritem_t packs
// the address in host order on the wire; the SDK GetIP() returns it as the
// network-order uint32 that inet_ntoa-style text expects in byte order
// low->high).
std::string formatServerIp(uint32_t ip);

// The engine connect command's payload for a server entry: "a.b.c.d:port"
// (the same string the shipped browser hands to "connect %s"). The port is
// the CONNECTION port; 0 falls back to the query port, and both 0 fail with
// an empty string (fail closed - never a connect to port 0).
std::string buildConnectAddress(const ServerEntry &entry);

// The key tuple Steam stores a record under. A zero queryPort is stored as
// the entry's connection port (the shipped read-back compares all three).
struct FavoriteKey {
  uint32_t ip = 0;
  uint16_t connPort = 0;
  uint16_t queryPort = 0;
  bool valid() const {
    return ip != 0 && connPort != 0;
  }
};
FavoriteKey favoriteKeyOf(const ServerEntry &entry);

} // namespace services::serverbrowser
