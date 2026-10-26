// Steam identity and avatars - direct connection to the game's
// steam_api.dll. from: ABCEnchance src/core/resource/playerresource.cpp +
// vgui_controls/avatar_image.cpp semantics, minus the import-library link:
// every export is bound with GetProcAddress so the plugin still loads
// (degraded) on setups without a live Steam client.
//
// Interface resolution rides the MODERN entry points (SteamAPI_GetHSteamUser
// + SteamInternal_FindOrCreateUserInterface with the header's interface
// version strings): current steam_api.dll builds dropped the flat
// SteamUser()/SteamFriends()/SteamUtils() C accessors (verified against the
// SC 5.26 runtime - 941 exports, accessors absent), so the old-code flat
// calls resolve nothing here. The game owns SteamAPI_Init/RunCallbacks, so
// this adapter only reads. Avatar download is the old poll-with-throttle
// shape: GetSmallFriendAvatar returns < 0 until Steam fetched the image,
// callers retry through steamapi::avatar().
//
// Animated avatars use the equipped-profile surface. Those flat exports only
// exist on Steamworks runtimes that know animated avatars, so they are bound
// by name (never called through an older vtable slot) and the whole feature
// degrades to Unavailable when any of them is missing. The APNG URL Steam
// returns is fetched through ISteamHTTP: SendHTTPRequest is asynchronous, the
// body is polled here, and the HUD thread never blocks on the network.

#include "SteamAPI.h"

#include "RuntimeAccess.h"

#include <steam/isteamfriends.h>
#include <steam/isteamhttp.h>
#include <steam/isteamuser.h>
#include <steam/isteamutils.h>

#include <Windows.h>

#include <string_view>
#include <unordered_map>
#include <vector>

namespace steamapi {

namespace {

bool &connected() {
  static bool instance = false;
  return instance;
}

ISteamUser *User = nullptr;
ISteamFriends *Friends = nullptr;
ISteamUtils *Utils = nullptr;
ISteamHTTP *Http = nullptr;

struct AvatarCacheEntry {
  int handle = 0; // steam image handle the buffer was decoded from
  std::vector<unsigned char> rgba;
  int width = 0;
  int height = 0;
  bool noAvatar =
      false; // steam answered "this user has no avatar" - stop polling
};
std::unordered_map<SteamId64, AvatarCacheEntry> AvatarCache;

// Flat exports of the optional equipped-profile surface. The signatures use
// uint64 SteamIDs, exactly like steam_api_flat.h; CSteamID is constructed by
// the flat wrapper.
using FnRequestEquippedProfileItems = SteamAPICall_t(__cdecl *)(ISteamFriends *,
                                                                uint64);
using FnHasEquippedProfileItem = bool(__cdecl *)(ISteamFriends *, uint64,
                                                 ECommunityProfileItemType);
using FnGetProfileItemPropertyString =
    const char *(__cdecl *)(ISteamFriends *, uint64, ECommunityProfileItemType,
                            ECommunityProfileItemProperty);

using FnCreateHTTPRequest = HTTPRequestHandle(__cdecl *)(ISteamHTTP *,
                                                         EHTTPMethod,
                                                         const char *);
using FnSetHTTPRequestNetworkActivityTimeout =
    bool(__cdecl *)(ISteamHTTP *, HTTPRequestHandle, uint32);
using FnSendHTTPRequest = bool(__cdecl *)(ISteamHTTP *, HTTPRequestHandle,
                                          SteamAPICall_t *);
using FnGetHTTPResponseBodySize = bool(__cdecl *)(ISteamHTTP *,
                                                  HTTPRequestHandle, uint32 *);
using FnGetHTTPResponseBodyData = bool(__cdecl *)(ISteamHTTP *,
                                                  HTTPRequestHandle, uint8 *,
                                                  uint32);
using FnReleaseHTTPRequest = bool(__cdecl *)(ISteamHTTP *, HTTPRequestHandle);

struct FlatApi {
  FnRequestEquippedProfileItems requestEquippedProfileItems = nullptr;
  FnHasEquippedProfileItem hasEquippedProfileItem = nullptr;
  FnGetProfileItemPropertyString profileItemPropertyString = nullptr;

  FnCreateHTTPRequest createHttpRequest = nullptr;
  FnSetHTTPRequestNetworkActivityTimeout setHttpTimeout = nullptr;
  FnSendHTTPRequest sendHttpRequest = nullptr;
  FnGetHTTPResponseBodySize responseBodySize = nullptr;
  FnGetHTTPResponseBodyData responseBodyData = nullptr;
  FnReleaseHTTPRequest releaseHttpRequest = nullptr;
};
FlatApi Flat;

bool ProfileItemsBound() {
  return Flat.requestEquippedProfileItems && Flat.hasEquippedProfileItem &&
         Flat.profileItemPropertyString;
}

bool HttpBound() {
  return Http && Flat.createHttpRequest && Flat.setHttpTimeout &&
         Flat.sendHttpRequest && Flat.responseBodySize &&
         Flat.responseBodyData && Flat.releaseHttpRequest;
}

bool connectInterfaces() {
  User = static_cast<ISteamUser *>(
      services::steam::findUserInterface(STEAMUSER_INTERFACE_VERSION));
  Friends = static_cast<ISteamFriends *>(
      services::steam::findUserInterface(STEAMFRIENDS_INTERFACE_VERSION));
  Utils = static_cast<ISteamUtils *>(
      services::steam::findUserInterface(STEAMUTILS_INTERFACE_VERSION));
  return User && Friends && Utils;
}

template <typename T> T bindExport(HMODULE module, const char *name) {
  return module ? reinterpret_cast<T>(GetProcAddress(module, name)) : nullptr;
}

void connectOptionalSurface() {
  const HMODULE module = GetModuleHandleA("steam_api.dll");
  Flat.requestEquippedProfileItems = bindExport<FnRequestEquippedProfileItems>(
      module, "SteamAPI_ISteamFriends_RequestEquippedProfileItems");
  Flat.hasEquippedProfileItem = bindExport<FnHasEquippedProfileItem>(
      module, "SteamAPI_ISteamFriends_BHasEquippedProfileItem");
  Flat.profileItemPropertyString = bindExport<FnGetProfileItemPropertyString>(
      module, "SteamAPI_ISteamFriends_GetProfileItemPropertyString");

  Http = static_cast<ISteamHTTP *>(
      services::steam::findUserInterface(STEAMHTTP_INTERFACE_VERSION));
  Flat.createHttpRequest = bindExport<FnCreateHTTPRequest>(
      module, "SteamAPI_ISteamHTTP_CreateHTTPRequest");
  Flat.setHttpTimeout = bindExport<FnSetHTTPRequestNetworkActivityTimeout>(
      module, "SteamAPI_ISteamHTTP_SetHTTPRequestNetworkActivityTimeout");
  Flat.sendHttpRequest = bindExport<FnSendHTTPRequest>(
      module, "SteamAPI_ISteamHTTP_SendHTTPRequest");
  Flat.responseBodySize = bindExport<FnGetHTTPResponseBodySize>(
      module, "SteamAPI_ISteamHTTP_GetHTTPResponseBodySize");
  Flat.responseBodyData = bindExport<FnGetHTTPResponseBodyData>(
      module, "SteamAPI_ISteamHTTP_GetHTTPResponseBodyData");
  Flat.releaseHttpRequest = bindExport<FnReleaseHTTPRequest>(
      module, "SteamAPI_ISteamHTTP_ReleaseHTTPRequest");
}

constexpr std::size_t kMaxAnimatedBytes = 8u * 1024u * 1024u;
constexpr std::size_t kMaxAnimatedEntries = 256;
constexpr double kNoneRetrySeconds = 300.0;
constexpr double kFetchTimeoutSeconds = 20.0;

double NowSeconds() {
  return static_cast<double>(GetTickCount64()) / 1000.0;
}

struct AnimatedEntry {
  enum class Phase { Probe, Fetching, Ready, None };
  Phase phase = Phase::Probe;
  bool requested = false;
  int probes = 0;
  double nextStep = 0.0;
  double deadline = 0.0;
  double retryAt = 0.0;
  HTTPRequestHandle request = 0;
  std::string url;
  std::vector<unsigned char> bytes;
};

std::unordered_map<SteamId64, AnimatedEntry> AnimatedCache;

void releaseRequest(AnimatedEntry &entry) {
  if (entry.request && HttpBound()) {
    Flat.releaseHttpRequest(Http, entry.request);
    entry.request = 0;
  }
}

AvatarAnimationState failProbe(AnimatedEntry &entry, double now) {
  releaseRequest(entry);
  entry.phase = AnimatedEntry::Phase::None;
  entry.retryAt = now + kNoneRetrySeconds;
  return AvatarAnimationState::None;
}

AvatarAnimationState animatedStep(SteamId64 id, AnimatedEntry &entry,
                                  AvatarAnimation &out, double now) {
  if (entry.phase == AnimatedEntry::Phase::Ready) {
    out.bytes = entry.bytes.data();
    out.size = entry.bytes.size();
    return AvatarAnimationState::Ready;
  }
  if (entry.phase == AnimatedEntry::Phase::None) {
    if (now < entry.retryAt)
      return AvatarAnimationState::None;
    entry.phase = AnimatedEntry::Phase::Probe;
    entry.requested = false;
    entry.probes = 0;
    entry.nextStep = now;
  }

  if (entry.phase == AnimatedEntry::Phase::Probe) {
    if (now < entry.nextStep)
      return AvatarAnimationState::Pending;
    // Ask Steam for the equipped items, then give the call result time to
    // reach the interface before reading it. One re-ask covers a slow
    // profile fetch; a second negative answer is treated as "no animated
    // avatar" and retried much later.
    if (!entry.requested) {
      entry.requested = true;
      entry.nextStep = now + 2.0;
      if (Friends)
        Friends->RequestUserInformation(CSteamID(id), false);
      if (Flat.requestEquippedProfileItems)
        Flat.requestEquippedProfileItems(Friends, id);
      return AvatarAnimationState::Pending;
    }
    entry.nextStep = now + 1.0;
    if (!Flat.hasEquippedProfileItem(
            Friends, id, k_ECommunityProfileItemType_AnimatedAvatar)) {
      if (entry.probes == 0) {
        ++entry.probes;
        entry.requested = false;
        return AvatarAnimationState::Pending;
      }
      return failProbe(entry, now);
    }

    const char *url = Flat.profileItemPropertyString(
        Friends, id, k_ECommunityProfileItemType_AnimatedAvatar,
        k_ECommunityProfileItemProperty_ImageLarge);
    if (!url || !*url)
      url = Flat.profileItemPropertyString(
          Friends, id, k_ECommunityProfileItemType_AnimatedAvatar,
          k_ECommunityProfileItemProperty_ImageSmall);
    if (!url || !*url)
      return failProbe(entry, now);

    entry.url = url;
    if (!HttpBound())
      return failProbe(entry, now);
    entry.request =
        Flat.createHttpRequest(Http, k_EHTTPMethodGET, entry.url.c_str());
    if (!entry.request)
      return failProbe(entry, now);
    Flat.setHttpTimeout(Http, entry.request, 15);
    SteamAPICall_t call = 0;
    if (!Flat.sendHttpRequest(Http, entry.request, &call)) {
      releaseRequest(entry);
      return failProbe(entry, now);
    }
    entry.phase = AnimatedEntry::Phase::Fetching;
    entry.deadline = now + kFetchTimeoutSeconds;
    return AvatarAnimationState::Pending;
  }

  // Fetching: ISteamHTTP runs the request; the body appears on the handle
  // once the response is complete. A body size query before completion is a
  // normal pending answer, so the deadline is the only failure signal.
  uint32 bodySize = 0;
  if (Flat.responseBodySize(Http, entry.request, &bodySize) && bodySize > 0) {
    if (bodySize > kMaxAnimatedBytes) {
      releaseRequest(entry);
      return failProbe(entry, now);
    }
    entry.bytes.resize(bodySize);
    if (!Flat.responseBodyData(Http, entry.request, entry.bytes.data(),
                               bodySize)) {
      releaseRequest(entry);
      return failProbe(entry, now);
    }
    releaseRequest(entry);
    entry.phase = AnimatedEntry::Phase::Ready;
    out.bytes = entry.bytes.data();
    out.size = entry.bytes.size();
    return AvatarAnimationState::Ready;
  }
  if (now >= entry.deadline) {
    releaseRequest(entry);
    return failProbe(entry, now);
  }
  return AvatarAnimationState::Pending;
}

} // namespace

void init() {
  if (connected() || !connectInterfaces())
    return;
  connectOptionalSurface();
  connected() = true;
}

void shutdown() {
  for (auto &[id, entry] : AnimatedCache)
    releaseRequest(entry);
  AnimatedCache.clear();
  AvatarCache.clear();
  User = nullptr;
  Friends = nullptr;
  Utils = nullptr;
  Http = nullptr;
  Flat = FlatApi();
  connected() = false;
}

bool available() {
  return connected();
}

SteamId64 localSteamId() {
  init();
  if (!available())
    return 0;
  if (!User)
    return 0;
  return User->GetSteamID().ConvertToUint64();
}

std::string personaName(SteamId64 id) {
  init();
  if (!available())
    return {};
  if (!Friends || !id)
    return {};
  if (Friends->RequestUserInformation(CSteamID(id), true))
    return {};
  const char *name = Friends->GetFriendPersonaName(CSteamID(id));
  if (name && (std::string_view(name) == "[unknown]" ||
               std::string_view(name) == "unknown"))
    return {};
  return name ? std::string(name) : std::string();
}

bool avatar(SteamId64 id, AvatarRgba &out) {
  out = AvatarRgba();
  init();
  if (!available())
    return false;
  if (!Friends || !Utils || !id)
    return false;
  if (Friends->RequestUserInformation(CSteamID(id), false))
    return false;
  if (!AvatarCache.count(id) && AvatarCache.size() >= 256)
    return false;
  AvatarCacheEntry &entry = AvatarCache[id];
  if (entry.noAvatar)
    return false;

  const int handle = Friends->GetSmallFriendAvatar(CSteamID(id));
  // Old LoadAvatarImage: > 0 decodes, == 0 means "user has no
  // avatar" (stop trying), < 0 means steam is still fetching.
  if (handle == 0) {
    entry.noAvatar = true;
    return false;
  }
  if (handle < 0) {
    // Nudge the download (old CAvatarImage
    // RequestUserInformation - non-friends need it for the
    // persona/avatar to arrive at all) and let the caller retry.
    Friends->RequestUserInformation(CSteamID(id), false);
    return false; // steam still downloading (old k_eAvatarPending path)
  }

  if (entry.handle == handle && !entry.rgba.empty()) {
    out.rgba = entry.rgba.data();
    out.width = entry.width;
    out.height = entry.height;
    return true;
  }

  uint32_t width = 0, height = 0;
  if (!Utils->GetImageSize(handle, &width, &height) || !width || !height ||
      width > 512 || height > 512)
    return false;
  // The header promises a returned pointer stays valid until shutdown.
  // Reserve the maximum avatar size once, then resize within that
  // capacity: a later avatar for the same SteamID changes the logical
  // size without reallocating, so earlier pointers remain valid.
  constexpr size_t kMaxAvatarBytes = 512u * 512u * 4u;
  if (entry.rgba.capacity() < kMaxAvatarBytes)
    entry.rgba.reserve(kMaxAvatarBytes);
  entry.rgba.resize(static_cast<size_t>(width) * height * 4);
  if (!Utils->GetImageRGBA(handle, entry.rgba.data(),
                           static_cast<int>(entry.rgba.size())))
    return false;
  entry.handle = handle;
  entry.width = static_cast<int>(width);
  entry.height = static_cast<int>(height);
  out.rgba = entry.rgba.data();
  out.width = entry.width;
  out.height = entry.height;
  return true;
}

AvatarAnimationState animatedAvatar(SteamId64 id, AvatarAnimation &out) {
  out = AvatarAnimation();
  init();
  if (!available())
    return AvatarAnimationState::Unavailable;
  if (!id)
    return AvatarAnimationState::None;
  if (!ProfileItemsBound() || !HttpBound() || !Friends)
    return AvatarAnimationState::Unavailable;
  if (!AnimatedCache.count(id) && AnimatedCache.size() >= kMaxAnimatedEntries)
    return AvatarAnimationState::None;
  AnimatedEntry &entry = AnimatedCache[id];
  return animatedStep(id, entry, out, NowSeconds());
}

void abandonAnimatedAvatar(SteamId64 id) {
  if (!available())
    return;
  const auto it = AnimatedCache.find(id);
  if (it == AnimatedCache.end())
    return;
  releaseRequest(it->second);
  if (it->second.phase == AnimatedEntry::Phase::Fetching) {
    it->second.phase = AnimatedEntry::Phase::Probe;
    it->second.requested = false;
    it->second.probes = 0;
    it->second.nextStep = NowSeconds() + 1.0;
  }
}

} // namespace steamapi
