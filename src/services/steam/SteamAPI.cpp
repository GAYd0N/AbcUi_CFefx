// rmlui::steamapi implementation - backend selection + the shared facade
// (see SteamAPI.h for the layering rationale).
#include "SteamAPI.h"

#include "SteamApiBackend.h"

namespace steamapi {

namespace {

SteamApiBackend *Active = nullptr;

}

void ensure();

void init() {
  if (Active)
    return;
  // Bridge first (the D-07 direction); its connect() only succeeds while
  // it carries the full consumer coverage, so the native backend serves
  // today and the bridge takes over when upstream catches up.
  for (SteamApiBackend *candidate : {bridgeBackend(), nativeBackend()}) {
    if (candidate->connect()) {
      Active = candidate;
      return;
    }
  }
}

void ensure() {
  // Lazy retry: the LoadClient probe can legitimately fail (steam_api.dll
  // not yet loaded / SteamAPI_Init pending); the first avatar or persona
  // consumer re-probes instead of degrading for the whole session.
  init();
}

void shutdown() {
  if (Active)
    Active->disconnect();
  Active = nullptr;
}

Backend backend() {
  return Active ? Active->kind() : Backend::None;
}

bool available() {
  return Active != nullptr;
}

SteamId64 localSteamId() {
  ensure();
  return Active ? Active->localSteamId() : 0;
}

std::string personaName(SteamId64 id) {
  ensure();
  return Active ? Active->personaName(id) : std::string();
}

bool avatar(SteamId64 id, AvatarRgba &out) {
  ensure();
  return Active ? Active->avatar(id, out) : false;
}

AvatarAnimationState animatedAvatar(SteamId64 id, AvatarAnimation &out) {
  ensure();
  return Active ? Active->animatedAvatar(id, out)
                : AvatarAnimationState::Unavailable;
}

void abandonAnimatedAvatar(SteamId64 id) {
  if (Active)
    Active->abandonAnimatedAvatar(id);
}

} // namespace steamapi
