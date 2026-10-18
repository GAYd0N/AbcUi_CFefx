#pragma once

// rmlui::steamapi - the virtual Steam access layer (user directive: keep the
// consumer surface backend-agnostic so the implementation can swap between
// the SteamAPIBridge plugin and a direct Steamworks connection later).
//
// Three layers:
//   - This header: the contract consumers code against (PlayerResource names,
//     Scoreboard avatars). No Steam types leak here.
//   - SteamApiNative / SteamApiBridge: two interchangeable backends behind
//     the same probe/contract shape (SteamApiBackend.h).
//   - Backend selection (SteamAPI.cpp): the bridge is probed first - the
//     original D-07 direction - but it only wins when it carries the FULL
//     capability set (SteamID + persona + avatar). The upstream C-ABI bridge
//     currently ships SteamID only, so in practice the native steam_api.dll
//     backend serves; when the bridge grows Friends/avatar coverage it takes
//     over without a consumer-side change.
//
// Every call is safe when Steam is absent (dedicated/uninitialized): values
// degrade (0 / empty / false), nothing crashes.

#include <string>
#include <cstdint>

namespace steamapi {

using SteamId64 = std::uint64_t;

enum class Backend {
  None,      // no usable provider
  NativeApi, // the game's steam_api.dll (ISteamUser/Friends/Utils)
  Bridge,    // MetaHookSv SteamAPIBridge.dll (C ABI)
};

// LoadClient time probe: binds whichever backend wins. Safe to re-call.
void init();
void shutdown();

Backend backend();
bool available();

// Local player's SteamID64; 0 when unavailable.
SteamId64 localSteamId();

// Friend persona name (the old "real name"); "" when unavailable/unknown.
std::string personaName(SteamId64 id);

// 32px avatar. False = not downloaded yet or unavailable; callers retry
// (the old CAvatarImage poll-with-throttle shape). The buffer is owned by
// the layer and valid until shutdown().
struct AvatarRgba {
  const unsigned char *rgba = nullptr;
  int width = 0;
  int height = 0;
};
bool avatar(SteamId64 id, AvatarRgba &out);

} // namespace steamapi
