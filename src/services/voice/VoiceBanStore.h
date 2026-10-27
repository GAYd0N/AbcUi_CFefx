#pragma once

// services::voice::VoiceBanStore - persistence adapter for the per-player
// voice mute list (issue 17: the legacy CVoiceBanMgr, new store).
//
// The voice_bans section in abc/abcui/config.json is written through the
// shared user-config owner and atomic sibling replacement.
//
// JSON keys and semantics - NEVER rename (persisted contract):
//   bans: array of SteamID64 values as DECIMAL STRINGS. The text form is
//   the lossless one (SteamID64 exceeds double precision) and stays
//   hand-editable; entries that are not canonical decimal ids are dropped
//   on load.
//
// Mutations persist immediately (the file is tiny and the mute toggle is
// the only writer). A damaged store loads empty and refuses saves for the
// session - never overwrite a damaged user file (the blacklist store's
// rule).

#include <cstdint>
#include <vector>

namespace services {
namespace voice {

class VoiceBanStore {
public:
  enum class LoadStatus {
    kLoaded,     // store read; bans adopted
    kMissing,    // no file yet (first run); empty list, saves allowed
    kUnreadable, // present but undecodable; empty list, saves refused
  };

  static LoadStatus load(std::vector<std::uint64_t> &out);
  // False when the write failed (the in-memory state still reflects it;
  // the next mutation retries).
  static bool save(const std::vector<std::uint64_t> &bans);
};

} // namespace voice
} // namespace services
