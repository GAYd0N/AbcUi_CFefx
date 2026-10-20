#pragma once

// services::playerlist - the T20 player-list domain: a pure snapshot/selection
// model over the AbcUi player resource (data::playerres) plus the muted-state
// readback discipline. Engine-free by design so the exact production logic
// links into the offline tests (05 doc tier 1); the module wires the seams.
//
// Identity contract (T20.a steps 1-2):
//   - A slot number is NOT an identity: the engine reuses slots. Every roster
//     snapshot carries, per occupied slot, the (steamId64, name) pair plus the
//     slot index; the SELECTION is keyed by that pair, so "same slot, new
//     player" invalidates the selection instead of silently retargeting an
//     action at the new occupant.
//   - The snapshot carries a session generation that advances whenever the
//     occupancy map (slot -> identity) changes. An action carries the
//     generation it was rendered from and is refused when the live one moved
//     (disconnect/reconnect/map change between render and click).
//   - A player with no SteamID (bots, LAN placeholders) keeps a slot-scoped
//     key (index + name); it can never be confused with a SteamID-bearing
//     identity because the key type is tagged.
//
// Mute contract (T20.a step 3):
//   - The muted state is ALWAYS read through the backend (`isMuted`); the model
//     never stores an optimistic value. A toggle only reports success when the
//     immediate read-back agrees with the requested state ("必须后端回读，
//     操作失败不先显示成功").
//   - The backend is fail-closed: `available()` false hides/disables the mute
//     surface entirely (no guessed command fallback, no global voice switch).
//   - Mutability follows the SHIPPED enable rule exactly (FUN_10015e30): the
//     backend must exist and the row must be a named, non-bot player. The
//     shipped dialog lets you select your own row (the backend decides); bots
//     (engine "*bot" value "1") and unnamed slots disable the button.
//
// Engine evidence (Ghidra, GameUI.dll image base 0x10000000) for the shipped
// player list this replaces:
//   - Row fill FUN_10015b80: for i in 1..GetMaxClients(), name =
//     PlayerInfo_ValueForKey(i, "name") (engine +0x170), row "index" = i
//     (1-based, the SAME value handed to the mute backend).
//   - CPlayerListDialog mute handler FUN_10015d70 (Command "Mute"): reads the
//     selected item's "index" key, then calls the interface returned by
//     FUN_100063f0() at vtable +8 (read), +0xc (mute), +0x10 (unmute), and
//     re-refreshes the list afterwards.
//   - The refresh FUN_10015fd0 calls the same +8 read to label each row
//     ("Muted"), and FUN_10015e30 enables the button only when the interface
//     exists and the slot is neither local nor a bot.
//   - That interface is resolved in CGameUI::Initialize (FUN_10006480) as
//     "GameClientExports001" from the client factory passed to GameUI.
//     Issue 17: the backend behind this model is now the ported legacy
//     voice service (services::voice, the persisted vban mute list) - the
//     client-factory probe died with it; the reference install never
//     served that interface anyway, so the read-back rules here are
//     unchanged and the mute surface finally works.

#include <cstdint>
#include <string>
#include <vector>

namespace services {
namespace playerlist {

// One occupied slot as the snapshot carries it. A value type: the engine-side
// playerres entry is re-read every frame, these copies never alias it.
struct RosterEntry {
  int index = 0;    // 1..32 entity slot
  std::string name; // engine/persona text, plain (escaped by the page)
  std::uint64_t steamId64 = 0; // 0 = no SteamID (LAN placeholder)
  bool local = false;          // the local player's slot
  bool spectating = false;
  bool bot = false;   // engine "*bot" value "1" (RCBot / built-in bots)
  bool muted = false; // only meaningful when the mute backend is available
};

// Selection key: SteamID-bearing identities key on the ID alone (a reconnect
// onto another slot keeps the same key); ID-less identities key on slot+name
// (they cannot be tracked across slots - honest, they have no stable ID).
struct SelectionKey {
  bool valid() const {
    return HasId || (Index != 0 && !Name.empty());
  }
  bool HasId = false;
  std::uint64_t SteamId64 = 0;
  int Index = 0;
  std::string Name;

  static SelectionKey forEntry(const RosterEntry &entry);
  bool matches(const RosterEntry &entry) const;
  bool operator==(const SelectionKey &other) const {
    return HasId == other.HasId && SteamId64 == other.SteamId64 &&
           Index == other.Index && Name == other.Name;
  }
};

enum class ToggleError {
  kNone,
  kUnavailable, // no mute backend on this install
  kNoSelection,
  kStale,           // the rendered generation no longer matches the live one
  kIdentityChanged, // the selected identity is gone / replaced
  kLocalPlayer,     // the local slot is not mutable
  kReadbackFailed,  // the backend refused or did not change state
};

class PlayerListModel {
public:
  // Engine seams. Plain function pointers, no captured state: production
  // wires thin thunks, the offline tests script them.
  struct Seams {
    // Fills `out` (capacity `max`) with the live occupied roster, returns
    // the count. Selection is preserved by the model, not the seam.
    int (*roster)(RosterEntry *out, int max) = nullptr;
    // Mute backend. `available` gates the whole surface; the three calls
    // are only consulted when it reports true. `isMuted` re-reads the
    // authoritative state; the toggles return whether the backend
    // accepted the request (the model still re-reads afterwards).
    bool (*muteAvailable)() = nullptr;
    bool (*isMuted)(const RosterEntry &entry) = nullptr;
    bool (*mute)(const RosterEntry &entry) = nullptr;
    bool (*unmute)(const RosterEntry &entry) = nullptr;
  };
  void setSeams(const Seams &seams) {
    Seams_ = seams;
  }

  // Re-reads the roster and revalidates the selection against it. Returns
  // true when anything the page projects changed (roster, mute states,
  // selection) - the page re-renders on true only. Idempotent per frame.
  bool refresh();

  const std::vector<RosterEntry> &roster() const {
    return Roster;
  }
  // Generation of the CURRENT snapshot (advances on any occupancy/identity
  // change; mute-state-only changes do not advance it - the mute toggle
  // revalidates by identity, not by generation, since muting does not move
  // anyone).
  std::uint32_t generation() const {
    return Generation;
  }
  bool muteAvailable() const {
    return MuteAvailable;
  }

  // Selection. `key` must carry a valid identity; a stale key is refused.
  void select(const SelectionKey &key);
  void clearSelection();
  const SelectionKey &selection() const {
    return Selection;
  }
  // The selected entry in the current snapshot (null when gone).
  const RosterEntry *selectedEntry() const;

  // Mute toggle for the current selection, revalidated at action time:
  // the identity must still occupy the snapshot, the mute backend must be
  // available, and the resulting state must read back before success is
  // reported. `mutedAfter` is the read-back state on kNone.
  ToggleError toggleMuted(bool &mutedAfter);

  // Row-count change detection for the page's projection signature.
  std::size_t revision() const {
    return Revision;
  }

private:
  void rebuildRoster();
  static bool sameOccupancy(const std::vector<RosterEntry> &a,
                            const std::vector<RosterEntry> &b);
  RosterEntry *findEntry(const SelectionKey &key);

  Seams Seams_;
  std::vector<RosterEntry> Roster;
  SelectionKey Selection;
  std::uint32_t Generation = 1;
  std::size_t Revision = 0;
  bool MuteAvailable = false;
  bool Primed = false;
};

} // namespace playerlist
} // namespace services
