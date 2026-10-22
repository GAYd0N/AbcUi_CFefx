// rmlui::steamapi native backend - direct connection to the game's
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
// this backend only reads. Avatar download is the old poll-with-throttle
// shape: GetSmallFriendAvatar returns < 0 until Steam fetched the image,
// callers retry through steamapi::avatar().

#include "SteamApiBackend.h"

#include "RuntimeAccess.h"

#include <steam/isteamuser.h>
#include <steam/isteamfriends.h>
#include <steam/isteamutils.h>

#include <abc/Console.h>

#include <unordered_map>
#include <vector>

namespace steamapi {

namespace {

ISteamUser *User = nullptr;
ISteamFriends *Friends = nullptr;
ISteamUtils *Utils = nullptr;

struct AvatarCacheEntry {
  int handle = 0; // steam image handle the buffer was decoded from
  std::vector<unsigned char> rgba;
  int width = 0;
  int height = 0;
  bool noAvatar =
      false; // steam answered "this user has no avatar" - stop polling
};
std::unordered_map<SteamId64, AvatarCacheEntry> AvatarCache;

bool connectInterfaces() {
  User = static_cast<ISteamUser *>(
      services::steam::findUserInterface(STEAMUSER_INTERFACE_VERSION));
  Friends = static_cast<ISteamFriends *>(
      services::steam::findUserInterface(STEAMFRIENDS_INTERFACE_VERSION));
  Utils = static_cast<ISteamUtils *>(
      services::steam::findUserInterface(STEAMUTILS_INTERFACE_VERSION));
  return User && Friends && Utils;
}

} // namespace

SteamApiBackend *nativeBackend() {
  static struct Backend final : SteamApiBackend {
    bool connect() override {

      return connectInterfaces();
    }

    void disconnect() override {
      AvatarCache.clear();
      User = nullptr;
      Friends = nullptr;
      Utils = nullptr;
    }

    steamapi::Backend kind() override {
      return steamapi::Backend::NativeApi;
    }

    SteamId64 localSteamId() override {
      if (!User)
        return 0;
      return User->GetSteamID().ConvertToUint64();
    }

    std::string personaName(SteamId64 id) override {
      if (!Friends || !id)
        return {};
      if (Friends->RequestUserInformation(CSteamID(id), true))
        return {};
      const char *name = Friends->GetFriendPersonaName(CSteamID(id));
      if (name &&
          (strcmp(name, "[unknown]") == 0 || strcmp(name, "unknown") == 0))
        return {};
      return name ? std::string(name) : std::string();
    }

    bool avatar(SteamId64 id, AvatarRgba &out) override {
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
  } instance;
  return &instance;
}

} // namespace steamapi
