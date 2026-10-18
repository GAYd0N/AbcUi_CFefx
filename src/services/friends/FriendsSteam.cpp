// services::friends - Steam adapter. Same rules as the other Steam readers in
// this plugin: the engine owns SteamAPI_Init/RunCallbacks (hw.dll), this code
// only reads through the interfaces resolved via
// SteamInternal_FindOrCreateUserInterface, and every value degrades to
// empty/false when Steam is absent.
//
// Address source, in order:
//   1. rich presence "connect" - the Source-family convention ("ip:port")
//      that the family of clients this project targets publishes; it is the
//      only source that survives a lobby/NAT setup;
//   2. FriendGameInfo_t's server address, which GoldSrc-family clients may
//      leave zeroed.
// Both are normalized here so the UI never sees a half address.

#include "FriendsModel.h"

#include <steam/isteamfriends.h>
#include <steam/isteamutils.h>

#include "../steam/RuntimeAccess.h"

#include <cstdio>

namespace services::friends {

namespace {

constexpr int kMaxFriendsScanned = 512;

std::string normalizeAddress(const char *text) {
  if (!text)
    return {};
  std::string value(text);
  // Strip a Steam URL prefix when present ("steam://connect/ip:port").
  const auto marker = value.find("connect/");
  if (marker != std::string::npos)
    value.erase(0, marker + 8);
  while (!value.empty() &&
         (value.back() == '\n' || value.back() == '\r' || value.back() == ' '))
    value.pop_back();
  // Keep the engine-visible grammar: host or host:port, nothing else.
  if (value.empty() || value.size() > 64)
    return {};
  for (const char c : value) {
    const bool allowed = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                         (c >= 'A' && c <= 'Z') || c == '.' || c == ':' ||
                         c == '-';
    if (!allowed)
      return {};
  }
  return value;
}

std::string addressFromGameInfo(uint32 ip, uint16 port) {
  if (ip == 0 || port == 0)
    return {};
  char text[32];
  std::snprintf(text, sizeof(text), "%u.%u.%u.%u:%u", (ip >> 24) & 0xFF,
                (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF, port);
  return text;
}

} // namespace

IFriendsProvider *steamFriendsProvider() {
  static struct Provider final : IFriendsProvider {
    ISteamFriends *Friends = nullptr;
    ISteamUtils *Utils = nullptr;

    bool bind() {
      // Re-resolve on every use: Steam may come up after the first UI
      // frame (interface lookups are cheap and cached inside Steam).
      if (!Friends)
        Friends = static_cast<ISteamFriends *>(
            services::steam::findUserInterface(STEAMFRIENDS_INTERFACE_VERSION));
      // Resolve Utils independently: SteamUtils can appear later than
      // SteamFriends, and the old nested lookup never retried it.
      if (!Utils)
        Utils = static_cast<ISteamUtils *>(
            services::steam::findUserInterface(STEAMUTILS_INTERFACE_VERSION));
      return Friends != nullptr;
    }

    bool available() override {
      return bind();
    }

    int count() override {
      if (!bind())
        return 0;
      const int total = Friends->GetFriendCount(k_EFriendFlagImmediate);
      return total > kMaxFriendsScanned ? kMaxFriendsScanned
                                        : (total > 0 ? total : 0);
    }

    bool entry(int index, FriendEntry &out) override {
      if (!bind() || index < 0)
        return false;
      out = FriendEntry{};
      const CSteamID id =
          Friends->GetFriendByIndex(index, k_EFriendFlagImmediate);
      if (id == k_steamIDNil)
        return false;
      out.SteamId = id.ConvertToUint64();
      const char *name = Friends->GetFriendPersonaName(id);
      out.Name = name ? name : "";

      FriendGameInfo_t info{};
      if (Friends->GetFriendGamePlayed(id, &info)) {
        out.InGame = true;
        // The board shows our game's name and, for every other game,
        // the "playing another game" fact - never the raw AppID.
        if (info.m_gameID.AppID() != 0 && Utils) {
          if (Utils->GetAppID() == info.m_gameID.AppID())
            out.Game = "Sven Co-op";
          else
            out.OtherGame = true;
        }
      }
      // Rich presence first: the client that publishes "connect" is the
      // one whose matchmaking the player actually used.
      out.Connect =
          normalizeAddress(Friends->GetFriendRichPresence(id, "connect"));
      if (out.Connect.empty() && out.InGame)
        out.Connect = addressFromGameInfo(info.m_unGameIP, info.m_usGamePort);
      return true;
    }
  } instance;
  return &instance;
}

} // namespace services::friends
