#pragma once

// services::video::Options - engine-independent <video> attribute validation.
// Mirrors rmlui/modelpreview/Options: the element supplies raw DOM
// attribute strings, this unit decides what is a known attribute, what is an
// error, and what the defaults are. No engine, no Rml, no FFmpeg here, so the
// offline tests cover it directly.

#include <string>
#include <string_view>

namespace video {

struct Options {
  // How the frame fills the element box when the aspects differ.
  enum class Fit {
    kCover,   // fill the box, crop the overflow (background usage)
    kContain, // fit inside the box, letterbox the remainder
    kFill,    // stretch to the box
  };

  // Video file path: mod-relative ("abcenchance/...") or absolute. Resolved
  // by the element (engine filesystem knowledge stays there).
  std::string Source;
  Fit FitMode = Fit::kCover;
  // A single playback speed multiplier (1.0 = authored speed).
  float Speed = 1.0f;
  bool Loop = true;
  bool Autoplay = true;

  // Unknown DOM attributes (id/class/style) are not errors. Invalid known
  // attributes leave the previous value unchanged and return false.
  bool set(std::string_view Name, std::string_view Value);

  static bool isVideoPath(std::string_view Path);
};

} // namespace video
