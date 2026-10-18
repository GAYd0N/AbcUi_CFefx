#pragma once

// services::video::Decoder - the AbcUi video frame source.
//
// Model: Renderer's startup video (src/startup_video.cpp) - Mile.FFmpeg.dll is
// loaded dynamically through the host search path (MetaHook appends
// metahook/dlls/* subdirectories to PATH), every entry point is bound with
// GetProcAddress and the ABI major version is verified before any struct is
// touched. No import library, no engine headers: this unit is a pure decoder
// so the offline tests can link it without a game.
//
// Responsibilities, deliberately narrow:
//   - open one file, decode one video stream;
//   - convert every decoded frame to RGBA8 at a caller-chosen output size
//     (aspect is the caller's business; the decoder scales to the exact box
//     it is given);
//   - own the media clock: the caller feeds it the frame delta, the decoder
//     answers whether the frame on screen must be refreshed.
// It never uploads textures, never knows about Rml documents and never reads
// the engine filesystem (paths are resolved by the caller).

#include <memory>
#include <string>

namespace video {

// Borrowed decoded pixels, top-left origin, tightly packed RGBA8. Valid until
// the next advance()/seek()/close() on the same decoder.
struct FrameView {
  const unsigned char *Pixels = nullptr;
  int Width = 0;
  int Height = 0;
};

class Decoder {
public:
  Decoder();
  ~Decoder();
  Decoder(const Decoder &) = delete;
  Decoder &operator=(const Decoder &) = delete;

  // Opens `path` (a filesystem path) and prepares an RGBA output of exactly
  // `outputWidth` x `outputHeight` (clamped to sane bounds). Re-opening
  // replaces the previous file. Returns false and leaves error() set on
  // failure; the caller decides whether that is fatal for the document.
  bool open(const char *path, int outputWidth, int outputHeight);
  void close();
  bool isOpen() const;

  // Advances the media clock and decodes whatever frames that clock has
  // passed. Returns true when a NEW frame is ready (the caller re-uploads);
  // false means "the previous frame is still current". `deltaSeconds` is the
  // engine frame time, so the video plays at wall-clock speed regardless of
  // the frame rate. A rate of zero (hidden document) costs nothing.
  bool advance(double deltaSeconds);

  // The current frame; null until the first advance() produced one.
  const FrameView &frame() const;

  // Restarts at the beginning once the end is reached (loop) or stops
  // decoding and keeps the last frame (no loop). Set before or after open().
  void setLooping(bool looping);
  bool looping() const;
  bool atEnd() const;

  // Source dimensions / duration in seconds (<= 0 when unknown).
  int sourceWidth() const;
  int sourceHeight() const;
  double duration() const;

  // True when the decode path is usable at all (Mile.FFmpeg loaded and the
  // ABI matched). Distinguishes "no SDK installed" from "this file failed".
  static bool available();

  // Presentation time of the frame currently on screen (< 0 before the first).
  double lastSeconds() const;

  const std::string &error() const;

private:
  struct State;
  std::unique_ptr<State> Data;
};

} // namespace video
