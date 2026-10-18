#pragma once

// services::menubackdrop::Backdrop - the shared menu backdrop.
//
// Every menu document paints over the same full-window image so a page never
// leaves undrawn (black) regions behind it: the live frame blurred by the
// Renderer's gaussian pass while a map is loaded, or a blurred background
// movie when the player is not in a game (menu rules 1 and 5 of the overhaul).
//
// Ownership, single instance per process:
//   - the FBO chain (one downscale target plus a blur ping-pong pair) and its
//     invalidation on vid restart / frame-buffer regeneration;
//   - the background movie decoder and its raw frame texture (video mode);
//   - the mode policy: kAuto resolves to kLive in a level and kVideo outside
//     one, falling back to kSolid when neither source is available.
// Consumers (the <backdrop> element, one per document) only read uri() and
// mirror it into an <img>; nothing else in the plugin touches the FBOs.
//
// Threading: engine thread only, called from the render-end beat (the blur
// needs the frame buffer that was just presented) and the HUD beat (video
// decode).

#include <memory>
#include <string>

namespace menubackdrop {

enum class Mode : int {
  kAuto = 0, // policy: live blur in a level, background video outside one
  kLive,     // always blur what the engine just rendered
  kVideo,    // always blur the background movie
  kSolid,    // flat colour (no source available; still covers the window)
};

class Backdrop {
public:
  Backdrop();
  ~Backdrop();
  Backdrop(const Backdrop &) = delete;
  Backdrop &operator=(const Backdrop &) = delete;

  // Requested mode (documents can pick a specific one; kAuto is the default).
  void setMode(Mode mode);
  Mode requestedMode() const;
  // Mode actually in use after resolution (diagnostics / element styling).
  Mode effectiveMode() const;

  // Background movie, mod-relative. Empty disables video mode.
  void setVideoSource(const std::string &source);
  const std::string &videoSource() const;

  // Blur rounds (1..4); each round is one horizontal + one vertical pass.
  void setBlurRounds(int rounds);
  int blurRounds() const;

  // HUD beat: decodes the background movie (video mode only).
  void pumpVideo(double frameTime, bool Enabled = true);
  // Render-end beat, before the Rml pass: captures the source, runs the blur
  // chain and publishes the result. Returns true when the published texture
  // changed (a new URI or a first-time registration).
  bool render();

  // The registered image URI for the current blurred frame; empty until the
  // first successful render.
  const std::string &uri() const;

  // Drops the FBO chain (vid restart / engine frame-buffer regeneration).
  void invalidateTargets();
  // Full reset (shutdown).
  void shutdown();

  struct Diagnostics {
    int Requested = 0;
    int Effective = 0;
    int Rounds = 0;
    int Width = 0;
    int Height = 0;
    bool VideoOpen = false;
    double VideoSeconds = 0.0;
  };
  Diagnostics diagnostics() const;

private:
  struct State;
  std::unique_ptr<State> Data;
};

// Process-scoped instance (the backdrop is one image shared by all documents).
Backdrop &menuBackdrop();

// Default background movie (shipped with the AbcEnchance assets).
constexpr const char *kDefaultVideoSource =
    "abc/abcui/gameui/scence/video/trainstation.mp4";

} // namespace menubackdrop
