// services::playerlist::muteBackend implementation - see MuteBackend.h.
//
// The adapter over services::voice::status() (issue 17): isMuted reads the
// ban list through the player's SteamID64 identity, mute/unmute set it and
// sync the server immediately. Refusals (identity-less players, the local
// player) come back false so the model's read-back reports honestly.
#include "MuteBackend.h"
#include "services/voice/VoiceStatus.h"

namespace services {
namespace playerlist {
namespace {

class Backend final : public IMuteBackend {
public:
  bool available() const override {
    return voice::status().available();
  }
  bool isMuted(int index) const override {
    return voice::status().isPlayerBlocked(index);
  }
  bool mute(int index) override {
    return voice::status().setPlayerBlockedState(index, true);
  }
  bool unmute(int index) override {
    return voice::status().setPlayerBlockedState(index, false);
  }
};

} // namespace

IMuteBackend *muteBackend() {
  static Backend backend;
  return &backend;
}

} // namespace playerlist
} // namespace services
