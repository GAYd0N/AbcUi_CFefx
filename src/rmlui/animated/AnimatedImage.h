#pragma once

// rmlui::animated::AnimatedImage - one animated image file, decoded by a
// format backend. The element (<animimg>, ElementAnimatedImage) owns one
// instance and drives playback; nothing here knows about RmlUi or the texture
// channel.
//
// Format support is a backend table: adding APNG (Steam animated avatars) is a
// new backend file plus one entry in AnimatedImage.cpp, never an element or
// caller change. A backend takes the resident file bytes (demux handles point
// into them) and reports the canvas size, the frame count and each frame's
// container duration.
//
// Contract:
//   - decode is sequential: Rewind() then Read() until Read() returns false.
//     The returned pixels stay valid until the next Read()/Rewind()/
//     destruction.
//   - pixels are top-down tightly packed RGBA8 with straight alpha, the layout
//     the VGUI2 texture upload channel and the renderer both take.
//   - DurationMs() is the container value; 0 means the container declares no
//     timing and the player picks a default.
//   - failures return false / null with a message in `error`; no exceptions
//     (the repository's parse-boundary rule).

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace rmlui::animated {

struct FrameView {
  const unsigned char *Pixels = nullptr;
  int Width = 0;
  int Height = 0;
  int DurationMs = 0;
  int Index = 0;
};

class AnimatedImage {
public:
  virtual ~AnimatedImage() = default;

  // Source diagnostics for the element's console warnings.
  const std::string &Source() const {
    return SourceName;
  }
  const char *Format() const {
    return FormatName;
  }

  int Width() const {
    return PixelWidth;
  }
  int Height() const {
    return PixelHeight;
  }
  int FrameCount() const {
    return FrameTotal;
  }
  int DurationMs(int index) const {
    return index >= 0 && index < static_cast<int>(Durations.size())
               ? Durations[index]
               : 0;
  }

  // Single-pass decode: Rewind() restarts at frame 0, every Read() yields the
  // next frame (FrameView::Index names it) and returns false at the end of the
  // sequence or on a decode error.
  virtual bool Rewind() = 0;
  virtual bool Read(FrameView &out) = 0;

protected:
  // Backend-filled metadata; the backend owns the decoder state itself.
  std::vector<unsigned char> Bytes;
  std::string SourceName;
  const char *FormatName = "unknown";
  int PixelWidth = 0;
  int PixelHeight = 0;
  int FrameTotal = 0;
  std::vector<int> Durations;
};

// Reads `path` through the engine filesystem (mod-relative, mounted *_addon /
// *_downloads paths included) and opens the animation.
std::unique_ptr<AnimatedImage> openFile(const std::string &path,
                                        std::string &error);

// In-memory source: Steam animated avatars arrive as bytes, never as files, so
// the format layer takes a buffer as well.
std::unique_ptr<AnimatedImage> openMemory(std::vector<unsigned char> bytes,
                                          std::string source,
                                          std::string &error);

namespace detail {

// One container decoder. Probe() runs on the leading bytes before Open();
// Open() takes ownership of the resident bytes.
struct Backend {
  const char *Name;
  bool (*Probe)(const unsigned char *data, std::size_t size);
  std::unique_ptr<AnimatedImage> (*Open)(std::vector<unsigned char> bytes,
                                         std::string source,
                                         std::string &error);
};

} // namespace detail

} // namespace rmlui::animated
