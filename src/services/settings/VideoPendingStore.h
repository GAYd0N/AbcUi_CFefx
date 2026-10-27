#pragma once

// services::settings::VideoPendingStore - persistence adapter for the T12
// unconfirmed-switch record (VideoModeModel.h protocol).
//
// The record is OUR schema (new contract, not an engine key): a small JSON
// section named video_pending in abc/abcui/config.json, written through the
// shared user-config owner and atomic sibling replacement.
// A cleared record is persisted with Pending=0 instead of removing the
// section - the read path treats any
// non-positive Pending as "no record", so the store needs no delete operation.
//
// JSON keys and semantics - NEVER rename (persisted contract; the offline
// test pins this exact serialization through aigc::JsonHelper):
//   pending, new_width, new_height, new_windowed,
//   old_width, old_height, old_windowed, applied_unix_ms, generation.

#include "VideoModeModel.h"

#include <AIGCJson.hpp>

namespace services {
namespace settings {

// The on-disk schema. Field order defines the written key order only; the
// parser is key-based. Mirrors VideoPendingRecord 1:1.
struct VideoPendingJson {
  int Pending = 0;
  int NewWidth = 0;
  int NewHeight = 0;
  int NewWindowed = 0;
  int OldWidth = 0;
  int OldHeight = 0;
  int OldWindowed = 0;
  long long AppliedUnixMs = 0;
  int Generation = 0;

  AIGC_JSON_HELPER(Pending, NewWidth, NewHeight, NewWindowed, OldWidth,
                   OldHeight, OldWindowed, AppliedUnixMs, Generation);
  AIGC_JSON_HELPER_RENAME("pending", "new_width", "new_height", "new_windowed",
                          "old_width", "old_height", "old_windowed",
                          "applied_unix_ms", "generation");
};

class VideoPendingStore {
public:
  // The VideoModeModel seams for production (savePending/loadPending).
  static bool save(const VideoPendingRecord &record);
  static bool load(VideoPendingRecord &out);
};

} // namespace settings
} // namespace services
