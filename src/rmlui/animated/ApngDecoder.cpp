// Animated PNG backend - see ApngDecoder.h.
#include "ApngDecoder.h"

#include "../../framework/ImageLoader.h"
#include "../../framework/ResourceLimits.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace rmlui::animated::detail {

namespace {

constexpr std::array<unsigned char, 8> kPngSignature = {0x89, 'P',  'N',  'G',
                                                        0x0D, 0x0A, 0x1A, 0x0A};
constexpr uint32_t kMaxFrames = 4096;
constexpr uint32_t kMaxChunkBytes = 64u * 1024u * 1024u;

uint32_t readU32(const std::byte *p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

uint16_t readU16(const std::byte *p) {
  return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) |
                               static_cast<uint16_t>(p[1]));
}

bool hasSignature(std::span<const std::byte> data) {
  return data.size() >= kPngSignature.size() &&
         std::memcmp(data.data(), kPngSignature.data(), kPngSignature.size()) ==
             0;
}

struct Chunk {
  const std::byte *Type = nullptr;
  std::span<const std::byte> Payload;
  std::size_t Next = 0;
};

// Walks one chunk at `offset`. The CRC is not validated: FreeImage checks the
// assembled frame PNG, and the container walk only needs the boundaries.
bool nextChunk(std::span<const std::byte> data, std::size_t offset,
               Chunk &out) {
  if (offset + 12 > data.size())
    return false;
  const uint32_t length = readU32(data.data() + offset);
  if (length > kMaxChunkBytes ||
      static_cast<std::size_t>(length) > data.size() - offset - 12)
    return false;
  out.Type = data.data() + offset + 4;
  out.Payload = data.subspan(offset + 8, length);
  out.Next = offset + 12 + length;
  return true;
}

bool chunkTypeIs(const Chunk &chunk, const char *type) {
  return std::memcmp(chunk.Type, type, 4) == 0;
}

uint32_t crc32(const std::byte *data, std::size_t size, uint32_t crc = 0) {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> values = {};
    for (uint32_t i = 0; i < values.size(); ++i) {
      uint32_t value = i;
      for (int bit = 0; bit < 8; ++bit)
        value = (value & 1u) ? 0xEDB88320u ^ (value >> 1) : value >> 1;
      values[i] = value;
    }
    return values;
  }();
  crc = ~crc;
  for (std::size_t i = 0; i < size; ++i)
    crc =
        table[(crc ^ static_cast<unsigned char>(data[i])) & 0xFFu] ^ (crc >> 8);
  return ~crc;
}

void appendU32(std::vector<std::byte> &out, uint32_t value) {
  out.push_back(static_cast<std::byte>((value >> 24) & 0xFFu));
  out.push_back(static_cast<std::byte>((value >> 16) & 0xFFu));
  out.push_back(static_cast<std::byte>((value >> 8) & 0xFFu));
  out.push_back(static_cast<std::byte>(value & 0xFFu));
}

void appendChunk(std::vector<std::byte> &out, const char type[4],
                 std::span<const std::byte> payload) {
  appendU32(out, static_cast<uint32_t>(payload.size()));
  const std::size_t typeStart = out.size();
  out.insert(out.end(), reinterpret_cast<const std::byte *>(type),
             reinterpret_cast<const std::byte *>(type) + 4);
  out.insert(out.end(), payload.begin(), payload.end());
  const uint32_t crc = crc32(out.data() + typeStart, 4 + payload.size());
  appendU32(out, crc);
}

struct Frame {
  int X = 0;
  int Y = 0;
  int Width = 0;
  int Height = 0;
  int DelayMs = 0;
  uint8_t Dispose = 0;
  uint8_t Blend = 0;
  std::vector<std::byte> Data;
};

class ApngAnimation final : public AnimatedImage {
public:
  bool open(std::vector<std::byte> bytes, std::string source,
            std::string &error) {
    Bytes = std::move(bytes);
    SourceName = std::move(source);
    FormatName = "apng";

    if (!parse(error))
      return false;
    if (PixelWidth <= 0 || PixelHeight <= 0 || Frames.empty()) {
      error = "apng has no frames";
      return false;
    }
    std::size_t canvasBytes = 0;
    if (!framework::ResourceLimits::imageBytes(PixelWidth, PixelHeight,
                                               canvasBytes)) {
      error = "apng canvas out of range";
      return false;
    }
    Canvas.assign(canvasBytes, 0);
    Backup.assign(canvasBytes, 0);
    FrameTotal = static_cast<int>(Frames.size());
    Durations.reserve(Frames.size());
    for (const Frame &frame : Frames)
      Durations.push_back(frame.DelayMs);
    return true;
  }

  bool Rewind() override {
    std::fill(Canvas.begin(), Canvas.end(), 0);
    NextIndex = 0;
    HasPrevious = false;
    PreviousDispose = 0;
    PreviousRect = {};
    return true;
  }

  bool Read(FrameView &out) override {
    if (NextIndex < 0 || NextIndex >= static_cast<int>(Frames.size()))
      return false;
    if (!Canvas.empty() && HasPrevious)
      disposePrevious();
    const Frame &frame = Frames[NextIndex];
    if (frame.Dispose == 2) {
      std::copy(Canvas.begin(), Canvas.end(), Backup.begin());
      PreviousHasBackup = true;
    } else {
      PreviousHasBackup = false;
    }

    std::vector<std::byte> png;
    if (!framePng(frame, png)) {
      NextIndex = 0;
      return false;
    }
    framework::ImageLoader::DecodedImage decoded;
    if (!framework::ImageLoader::loadFromMemory(png, decoded) ||
        decoded.Width != frame.Width || decoded.Height != frame.Height) {
      NextIndex = 0;
      return false;
    }

    composite(frame, decoded);
    PreviousDispose = frame.Dispose;
    PreviousRect = {frame.X, frame.Y, frame.Width, frame.Height};
    HasPrevious = true;
    out.Pixels = Canvas.data();
    out.Width = PixelWidth;
    out.Height = PixelHeight;
    out.DurationMs = frame.DelayMs;
    out.Index = NextIndex;
    ++NextIndex;
    return true;
  }

private:
  struct Rect {
    int X = 0;
    int Y = 0;
    int Width = 0;
    int Height = 0;
  };

  bool framePng(const Frame &frame, std::vector<std::byte> &out) const {
    out.clear();
    out.reserve(frame.Data.size() + 128);
    out.insert(out.end(),
               reinterpret_cast<const std::byte *>(kPngSignature.data()),
               reinterpret_cast<const std::byte *>(kPngSignature.data()) +
                   kPngSignature.size());
    std::vector<std::byte> ihdr;
    appendU32(ihdr, static_cast<uint32_t>(frame.Width));
    appendU32(ihdr, static_cast<uint32_t>(frame.Height));
    const std::array<std::byte, 5> tail = {static_cast<std::byte>(BitDepth),
                                           static_cast<std::byte>(ColorType),
                                           static_cast<std::byte>(Compression),
                                           static_cast<std::byte>(FilterMethod),
                                           static_cast<std::byte>(Interlace)};
    ihdr.insert(ihdr.end(), tail.begin(), tail.end());
    appendChunk(out, "IHDR", ihdr);
    if (!Palette.empty())
      appendChunk(out, "PLTE", Palette);
    if (!Transparency.empty())
      appendChunk(out, "tRNS", Transparency);
    appendChunk(out, "IDAT", frame.Data);
    appendChunk(out, "IEND", {});
    return true;
  }

  void disposePrevious() {
    if (PreviousRect.Width <= 0 || PreviousRect.Height <= 0)
      return;
    if (PreviousDispose == 1) {
      fillRect(PreviousRect);
    } else if (PreviousDispose == 2 && PreviousHasBackup) {
      Canvas = Backup;
    }
  }

  void fillRect(const Rect &rect) {
    const int x0 = std::clamp(rect.X, 0, PixelWidth);
    const int y0 = std::clamp(rect.Y, 0, PixelHeight);
    const int x1 = std::clamp(rect.X + rect.Width, 0, PixelWidth);
    const int y1 = std::clamp(rect.Y + rect.Height, 0, PixelHeight);
    for (int y = y0; y < y1; ++y) {
      unsigned char *row =
          Canvas.data() + (static_cast<std::size_t>(y) * PixelWidth + x0) * 4;
      for (int x = x0; x < x1; ++x, row += 4) {
        row[0] = 0;
        row[1] = 0;
        row[2] = 0;
        row[3] = 0;
      }
    }
  }

  void composite(const Frame &frame,
                 const framework::ImageLoader::DecodedImage &decoded) {
    const int x0 = std::max(0, frame.X);
    const int y0 = std::max(0, frame.Y);
    const int x1 = std::min(PixelWidth, frame.X + frame.Width);
    const int y1 = std::min(PixelHeight, frame.Y + frame.Height);
    for (int y = y0; y < y1; ++y) {
      const unsigned char *src =
          decoded.Pixels.data() +
          (static_cast<std::size_t>(y - frame.Y) * decoded.Width) * 4;
      unsigned char *dst =
          Canvas.data() + (static_cast<std::size_t>(y) * PixelWidth + x0) * 4;
      for (int x = x0; x < x1; ++x, src += 4, dst += 4) {
        if (frame.Blend == 0) {
          std::memcpy(dst, src, 4);
          continue;
        }
        const unsigned sa = src[3];
        if (sa == 255 || dst[3] == 0) {
          if (sa == 0)
            continue;
          std::memcpy(dst, src, 4);
          continue;
        }
        if (sa == 0)
          continue;
        // Straight-alpha source-over, normalized. Double precision keeps the
        // rounding within the last bit of the reference decoder; the image is
        // a small avatar/canvas, so the per-pixel cost is negligible.
        const double sourceAlpha = sa / 255.0;
        const double destAlpha = dst[3] / 255.0;
        const double outAlpha = sourceAlpha + destAlpha * (1.0 - sourceAlpha);
        if (outAlpha <= 0.0) {
          std::memset(dst, 0, 4);
          continue;
        }
        for (int c = 0; c < 3; ++c) {
          const double value = (src[c] * sourceAlpha +
                                dst[c] * destAlpha * (1.0 - sourceAlpha)) /
                               outAlpha;
          dst[c] = static_cast<unsigned char>(
              std::lround(std::clamp(value, 0.0, 255.0)));
        }
        dst[3] = static_cast<unsigned char>(
            std::lround(std::clamp(outAlpha * 255.0, 0.0, 255.0)));
      }
    }
  }

  bool parse(std::string &error) {
    if (!hasSignature(Bytes)) {
      error = "apng signature rejected";
      return false;
    }
    bool sawHeader = false;
    bool sawAnimation = false;
    bool sawFrameControl = false;
    std::size_t offset = kPngSignature.size();
    Frame current;
    bool haveFrame = false;

    const auto finishFrame = [&]() {
      if (!haveFrame)
        return true;
      if (current.Width <= 0 || current.Height <= 0 || current.Data.empty()) {
        error = "apng frame has no data";
        return false;
      }
      if (Frames.size() >= kMaxFrames) {
        error = "apng frame count out of range";
        return false;
      }
      Frames.push_back(std::move(current));
      current = Frame();
      haveFrame = false;
      return true;
    };

    while (offset < Bytes.size()) {
      Chunk chunk;
      if (!nextChunk(Bytes, offset, chunk)) {
        error = "apng chunk bounds rejected";
        return false;
      }
      offset = chunk.Next;
      if (chunkTypeIs(chunk, "IHDR")) {
        if (chunk.Payload.size() != 13) {
          error = "apng IHDR size rejected";
          return false;
        }
        PixelWidth = static_cast<int>(readU32(chunk.Payload.data()));
        PixelHeight = static_cast<int>(readU32(chunk.Payload.data() + 4));
        BitDepth = static_cast<uint8_t>(chunk.Payload[8]);
        ColorType = static_cast<uint8_t>(chunk.Payload[9]);
        Compression = static_cast<uint8_t>(chunk.Payload[10]);
        FilterMethod = static_cast<uint8_t>(chunk.Payload[11]);
        Interlace = static_cast<uint8_t>(chunk.Payload[12]);
        if (BitDepth == 0 || Compression != 0 || FilterMethod != 0 ||
            Interlace > 1) {
          error = "apng IHDR fields rejected";
          return false;
        }
        sawHeader = true;
      } else if (chunkTypeIs(chunk, "acTL")) {
        if (chunk.Payload.size() != 8 || !sawHeader) {
          error = "apng acTL rejected";
          return false;
        }
        sawAnimation = true;
      } else if (chunkTypeIs(chunk, "PLTE")) {
        Palette.assign(chunk.Payload.begin(), chunk.Payload.end());
      } else if (chunkTypeIs(chunk, "tRNS")) {
        Transparency.assign(chunk.Payload.begin(), chunk.Payload.end());
      } else if (chunkTypeIs(chunk, "fcTL")) {
        if (chunk.Payload.size() != 26) {
          error = "apng fcTL size rejected";
          return false;
        }
        if (!finishFrame())
          return false;
        current.Width = static_cast<int>(readU32(chunk.Payload.data() + 4));
        current.Height = static_cast<int>(readU32(chunk.Payload.data() + 8));
        current.X = static_cast<int>(readU32(chunk.Payload.data() + 12));
        current.Y = static_cast<int>(readU32(chunk.Payload.data() + 16));
        const uint16_t delayNum = readU16(chunk.Payload.data() + 20);
        const uint16_t delayDen = readU16(chunk.Payload.data() + 22);
        const uint32_t milliseconds = static_cast<uint32_t>(delayNum) * 1000u /
                                      (delayDen == 0 ? 100u : delayDen);
        current.DelayMs = static_cast<int>(std::min<uint32_t>(
            milliseconds,
            static_cast<uint32_t>(std::numeric_limits<int>::max())));
        current.Dispose = static_cast<uint8_t>(chunk.Payload[24]);
        current.Blend = static_cast<uint8_t>(chunk.Payload[25]);
        if (current.Dispose > 2 || current.Blend > 1) {
          error = "apng fcTL operation rejected";
          return false;
        }
        sawFrameControl = true;
        haveFrame = true;
      } else if (chunkTypeIs(chunk, "IDAT")) {
        // An APNG whose first fcTL follows IDAT keeps the default image out
        // of the animation (the frames start at the first fcTL/fdAT pair).
        if (!sawFrameControl) {
          // Default image only: not part of the animation.
        } else {
          if (!haveFrame) {
            current.X = 0;
            current.Y = 0;
            current.Width = PixelWidth;
            current.Height = PixelHeight;
            haveFrame = true;
          }
          current.Data.insert(current.Data.end(), chunk.Payload.begin(),
                              chunk.Payload.end());
        }
      } else if (chunkTypeIs(chunk, "fdAT")) {
        if (!haveFrame || chunk.Payload.size() < 4) {
          error = "apng fdAT without frame control";
          return false;
        }
        current.Data.insert(current.Data.end(), chunk.Payload.begin() + 4,
                            chunk.Payload.end());
      } else if (chunkTypeIs(chunk, "IEND")) {
        break;
      }
    }
    if (!sawHeader || !finishFrame()) {
      error = "apng header/frame rejected";
      return false;
    }
    if (!sawAnimation || !sawFrameControl || Frames.empty()) {
      error = "png is not an apng";
      return false;
    }
    return true;
  }

  std::vector<Frame> Frames;
  std::vector<std::byte> Palette;
  std::vector<std::byte> Transparency;
  std::vector<unsigned char> Canvas;
  std::vector<unsigned char> Backup;
  Rect PreviousRect;
  uint8_t PreviousDispose = 0;
  bool HasPrevious = false;
  bool PreviousHasBackup = false;
  int NextIndex = 0;
  uint8_t BitDepth = 8;
  uint8_t ColorType = 6;
  uint8_t Compression = 0;
  uint8_t FilterMethod = 0;
  uint8_t Interlace = 0;
};

std::unique_ptr<AnimatedImage>
openApng(std::vector<std::byte> bytes, std::string source, std::string &error) {
  auto animation = std::make_unique<ApngAnimation>();
  if (!animation->open(std::move(bytes), std::move(source), error))
    return nullptr;
  return animation;
}

} // namespace

bool isAnimatedPng(std::span<const std::byte> bytes) {
  if (!hasSignature(bytes))
    return false;
  std::size_t offset = kPngSignature.size();
  while (offset < bytes.size()) {
    Chunk chunk;
    if (!nextChunk(bytes, offset, chunk))
      return false;
    if (chunkTypeIs(chunk, "acTL"))
      return true;
    if (chunkTypeIs(chunk, "IDAT") || chunkTypeIs(chunk, "IEND"))
      return false;
    offset = chunk.Next;
  }
  return false;
}

const Backend &apngBackend() {
  static const Backend backend{"apng", &isAnimatedPng, &openApng};
  return backend;
}

} // namespace rmlui::animated::detail
