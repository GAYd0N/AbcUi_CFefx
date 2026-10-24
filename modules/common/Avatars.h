#pragma once

#include <abcui/module_api.h>

#include <string>

namespace moduleui {
inline std::string avatarUri(const AbcUiModuleApi *api, uint64_t steamId,
                             const char *fallback) {
  int32_t generation = 0;
  if (api->players->avatar(steamId, &generation) != ABCUI_OK)
    return fallback;
  return "avatar://" + std::to_string(steamId) +
         "?gen=" + std::to_string(generation);
}

// Animated-avatar URI for a row whose avatar is a cached Steam APNG. Uses the
// appended players entry, so a module can still run on a framework whose
// players table predates it (static avatar fallback).
inline bool animatedAvatarUri(const AbcUiModuleApi *api, uint64_t steamId,
                              std::string &out) {
  if (!ABCUI_API_HAS_MEMBER(AbcUiPlayersApi, api->players, avatarAnimated))
    return false;
  int32_t generation = 0;
  if (api->players->avatarAnimated(steamId, &generation) != ABCUI_OK)
    return false;
  out = "avataranim://" + std::to_string(steamId) +
        "?gen=" + std::to_string(generation);
  return true;
}
} // namespace moduleui
