#pragma once

// services::playerlist::muteBackend - the player-mute backend, resolved the
// way the SHIPPED player list resolves it (Ghidra evidence in
// PlayerListModel.h): CGameUI::Initialize asks the client factory for
// "GameClientExports001" and caches the pointer (GameUI.dll FUN_10006480,
// global 0x100a8454); the dialog then calls that interface's vtable slots
// +8 (IsPlayerMuted), +0xc (mute), +0x10 (unmute) with the 1-based row
// "index" (FUN_10015d70 / FUN_10015fd0 / FUN_10015e30).
//
// This module resolves the SAME interface through the engine client factory
// (GetClientFactory -> the client DLL's CreateInterface), NOT by reading
// GameUI's private global - the interface is engine-visible and the shipped
// behaviour is a pure consequence of it. Availability is therefore a runtime
// probe: on an install whose client DLL does not serve GameClientExports001
// the probe answers false and the mute surface is disabled (fail closed) -
// which is EXACTLY what the shipped dialog does in that case: its enable rule
// (FUN_10015e30 tail) is `if (interface != 0 && !notMutable) enable(1) else
// enable(0)`, so a null interface disabled the original button too. Live
// probe on this install (T20): the name is not served while SCClientDLL001
// through the same factory resolves - the factory path works, the interface
// does not exist here. No guessed command, no global voice cvar, no memory
// patching.
//
// The +8/+0xc/+0x10 slot ORDER is the shipped call order (three consecutive
// slots right after the vtable head); the semantic names are the shipped
// dialog's use of them. Calls carry only the slot index - the callee keeps
// whatever per-player state it has (the shipped client voice system), so a
// failed call is a refused call, never a silent one.

#include <cstdint>

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

// Process-lifetime singleton; resolves lazily on first use (the client
// factory is stable from LoadClient on, but staying lazy keeps the probe
// honest about when the answer was obtained).
IMuteBackend *muteBackend();

} // namespace playerlist
} // namespace services
