#pragma once

// services::playerlist::muteBackend - the player-mute backend.
//
// Issue 17: the shipped probe (the client factory's "GameClientExports001"
// vtable, see git history) is replaced by an adapter over
// services::voice::status() - the ported legacy CVoiceStatus. That service
// owns the persisted SteamID64 ban list and mirrors it to the server
// (vban/VModEnable); this surface is a thin 1-based-entity-slot view of it
// for the player list model. Availability is the service's init state: the
// voice path needs no client interface, only the hook spine.
//
// Read-back discipline (inherited from the shipped dialog): mute/unmute
// report whether the service accepted the change; the caller re-reads
// isMuted and shows THAT - never an optimistic flip.

namespace services {
namespace playerlist {

// One resolved mute backend. All methods are safe when unavailable (false).
class IMuteBackend {
public:
  virtual ~IMuteBackend() = default;
  virtual bool available() const = 0;
  // 1-based entity slot, exactly the value the shipped row stores as
  // "index" and hands to the interface.
  virtual bool isMuted(int index) const = 0;
  virtual bool mute(int index) = 0;
  virtual bool unmute(int index) = 0;
};

// Process-lifetime singleton.
IMuteBackend *muteBackend();

} // namespace playerlist
} // namespace services
