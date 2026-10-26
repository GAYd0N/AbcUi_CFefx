#pragma once

// Steam identity and avatars, bound directly to the host's steam_api.dll.
// Consumers use this Steam-type-free contract; RuntimeAccess shares the
// same interface resolution path with server discovery and friends.
// The engine owns Steam initialization, shutdown and callback pumping.
//
// Every call is safe when Steam is absent (dedicated/uninitialized): values
// degrade (0 / empty / false), nothing crashes.

#include <cstddef>
#include <cstdint>
#include <string>

namespace steamapi {

using SteamId64 = std::uint64_t;

// LoadClient time probe of the host Steam runtime. Safe to re-call; consumers
// retry lazily if the DLL or Steam user was not ready during LoadClient.
void init();
void shutdown();

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

// Steam animated avatars (APNG). The equipped-profile surface is optional
// (the flat SteamAPI_ISteamFriends_* exports are probed dynamically): an old
// Steam runtime reports Unavailable and the caller keeps the static avatar.
struct AvatarAnimation {
  const unsigned char *bytes = nullptr;
  std::size_t size = 0;
};

enum class AvatarAnimationState {
  Unavailable, // no profile-item/HTTP surface on this runtime
  None,        // profile answered: this user has no animated avatar
  Pending,     // probe or download in flight; ask again later
  Ready,       // bytes holds a complete APNG
};

// Poll one avatar's animated state. Does at most one throttled step per call
// (request the equipped items, query the image URL, or poll the download), so
// the HUD thread never blocks on Steam or the network. The returned bytes stay
// valid until shutdown() or disconnect(); the avatar cache copies them.
AvatarAnimationState animatedAvatar(SteamId64 id, AvatarAnimation &out);

// Cancels an in-flight animation download (the scoreboard closed). Safe for
// ids that are not downloading.
void abandonAnimatedAvatar(SteamId64 id);

} // namespace steamapi
