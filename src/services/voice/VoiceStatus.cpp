// services::voice implementation - see the header.
#include "VoiceStatus.h"

#include "VoiceBanStore.h"
#include "data/NetworkMessages.h"
#include "data/PlayerResource.h"

#include <abc/CvarCmd.h>

#include <metahook.h>

#include <algorithm>
#include <cstdio>

namespace services {
namespace voice {
namespace {
constexpr int kMaxPlayers = 32;
// The sync throttle: one server sync per second of client time (the
// legacy CVoiceStatus::HUD_Frame beat).
constexpr float kSyncInterval = 1.0f;
} // namespace

VoiceStatus &status() {
  static VoiceStatus instance;
  return instance;
}

void VoiceStatus::init() {
  if (Initialized_)
    return;
  Initialized_ = true;
  const auto loaded = VoiceBanStore::load(Bans_);
  if (loaded == VoiceBanStore::LoadStatus::kUnreadable)
    abc::ConsoleWarn("[AbcUi] voice ban store damaged; mutes are read-only "
                     "this session\n");
  netmsg::VoiceMask.append(
      [](const netmsg::VoiceMaskData &data) { status().onVoiceMask(data); });
  netmsg::ReqState.append([]() { status().onReqState(); });
}

void VoiceStatus::frame() {
  if (!Initialized_)
    return;
  if (gEngfuncs.GetClientTime() - LastSyncTime_ > kSyncInterval)
    updateServerState(false);
}

void VoiceStatus::resetForWorld() {
  // A map change invalidates every slot the acked mask referred to; the
  // ban list itself is identity-keyed and stays. The server re-acks (and
  // requests our state) on connect.
  AckedBanMask_ = 0;
  ServerModEnable_ = -1;
  LastSyncTime_ = 0.0f;
}

void VoiceStatus::shutdown() {
  if (!Initialized_)
    return;
  if (!Bans_.empty())
    VoiceBanStore::save(Bans_);
}

bool VoiceStatus::isPlayerBlocked(int index) const {
  const auto *info = playerres::getPlayerInfo(index);
  if (!info || !info->isValid() || info->steamId64 == 0)
    return false;
  return isBanned(info->steamId64);
}

bool VoiceStatus::setPlayerBlockedState(int index, bool blocked) {
  const auto *info = playerres::getPlayerInfo(index);
  if (!info || !info->isValid() || info->steamId64 == 0)
    return false;
  if (info->isThisPlayer())
    return false; // the local player is never mutable
  const auto itor =
      std::lower_bound(Bans_.begin(), Bans_.end(), info->steamId64);
  const bool present = itor != Bans_.end() && *itor == info->steamId64;
  if (blocked != present) {
    if (blocked)
      Bans_.insert(itor, info->steamId64);
    else
      Bans_.erase(itor);
    VoiceBanStore::save(Bans_);
  }
  updateServerState(false);
  return true;
}

void VoiceStatus::onVoiceMask(const netmsg::VoiceMaskData &data) {
  AckedBanMask_ = data.serverBanned;
  ServerModEnable_ = data.modEnable;
}

void VoiceStatus::onReqState() {
  // The server asked for our state right after connect: send everything.
  updateServerState(true);
}

void VoiceStatus::updateServerState(bool force) {
  const char *levelName = gEngfuncs.pfnGetLevelName();
  if (!levelName || *levelName == '\0')
    return; // outside a level there is nobody to talk to; retry next tick
  const int cvarModEnable =
      static_cast<int>(abc::CVAR_GET_FLOAT("voice_modenable"));
  if (force || ServerModEnable_ != cvarModEnable) {
    ServerModEnable_ = cvarModEnable;
    char command[64];
    std::snprintf(command, sizeof(command), "VModEnable %d", ServerModEnable_);
    gEngfuncs.pfnServerCmd(command);
  }
  unsigned long banMask = 0;
  for (int i = 1; i <= kMaxPlayers; ++i) {
    const auto *info = playerres::getPlayerInfo(i);
    if (!info || !info->isValid() || info->steamId64 == 0)
      continue;
    if (isBanned(info->steamId64))
      banMask |= 1ul << (i - 1);
  }
  // The acked mask is the only comparison point: while it disagrees the
  // tick keeps re-sending (a lost unreliable packet heals within a second).
  if (!force && banMask == AckedBanMask_)
    return;
  char command[48];
  std::snprintf(command, sizeof(command), "vban %lx", banMask);
  gEngfuncs.pfnServerCmdUnreliable(command);
  LastSyncTime_ = gEngfuncs.GetClientTime();
}

bool VoiceStatus::isBanned(std::uint64_t steamId64) const {
  return std::binary_search(Bans_.begin(), Bans_.end(), steamId64);
}

} // namespace voice
} // namespace services
