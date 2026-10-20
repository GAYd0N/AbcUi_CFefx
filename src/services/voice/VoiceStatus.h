#pragma once

// services::voice - per-player voice blocking (issue 17: the legacy
// CVoiceStatus port, new store).
//
// The client owns its mute list (SteamID64-keyed, persisted by
// VoiceBanStore at <mod>/abc/abcui/voice_ban.dt) and mirrors it to the
// server as a 32-slot vban bitmask (bit i-1 == entity index i): VModEnable
// rides the reliable channel whenever voice_modenable flips, vban the
// unreliable one on every diff against the server's ack, re-synced once
// per second and on the server's ReqState. The ack (the VoiceMask usermsg)
// is the only truth the sync compares against, so a dropped unreliable
// packet heals on the next tick - which is also why mute state must be
// READ BACK from the ban list, never shown optimistically.
//
// Identity comes from playerres::getPlayerInfo(index)->steamId64: players
// without a SteamID have no identity to ban and every query answers false.
// The local player can never be muted through this service.
//
// Lifecycle (the hook spine, once per DLL): init at HUD_Init (loads the
// store, subscribes VoiceMask/ReqState), frame() each HUD_Frame (1 s
// throttle), resetForWorld at HUD_VidInit (transient state cleared, bans
// kept), shutdown at HUD_Shutdown (store persisted; every toggle already
// saved).

#include <cstdint>
#include <vector>

namespace netmsg {
struct VoiceMaskData;
}

namespace services {
namespace voice {

class VoiceStatus {
public:
  void init();
  void frame();
  void resetForWorld();
  void shutdown();

  // 1..32 entity index. Blocked == the player's SteamID64 is in the ban
  // store; identity-less players always answer false.
  bool isPlayerBlocked(int index) const;
  // Sets the ban and syncs the server immediately. False when the player
  // has no identity (not connected, SteamID-less or the local player) -
  // callers must not report success.
  bool setPlayerBlockedState(int index, bool blocked);
  // True once init() ran (the store is up and the messages are hooked).
  bool available() const {
    return Initialized_;
  }

  // netmsg feed (subscribed by init()).
  void onVoiceMask(const netmsg::VoiceMaskData &data);
  void onReqState();

private:
  // The legacy UpdateServerState: VModEnable on the reliable channel when
  // forced or the cvar moved, vban on the unreliable one when the local
  // mask differs from the server's ack. No-ops outside a level; the sync
  // throttle lives in frame().
  void updateServerState(bool force);
  bool isBanned(std::uint64_t steamId64) const;

  std::vector<std::uint64_t> Bans_; // sorted, unique
  unsigned long AckedBanMask_ = 0;  // the server's last VoiceMask word
  int ServerModEnable_ = -1;        // -1: the first tick always sends
  float LastSyncTime_ = 0.0f;
  bool Initialized_ = false;
};

// Process-lifetime service (the engine calls back through the hook spine;
// the DLL lifetime is the service lifetime). Consume through this
// accessor, never a file-level static.
VoiceStatus &status();

} // namespace voice
} // namespace services
