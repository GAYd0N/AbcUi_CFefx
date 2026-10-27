// Mp3Duration implementation - see Mp3Duration.h.
#include "Mp3Duration.h"

#include <cstring>

namespace mediaplayer {
namespace {

constexpr std::size_t kMaxFrames = 2000000;
// ID3v2 tags can carry large artwork; the first frame search is bounded so a
// malformed tag cannot make one page open scan a whole file byte by byte.
constexpr std::size_t kMaxTagSkip = 64 * 1024;

struct Header {
  int version = 0; // 1, 2 (MPEG-2) or 25 (MPEG-2.5); MPEG-1 is 1 as well.
  int layer = 0;
  int bitrateKbps = 0;
  int sampleRate = 0;
  int padding = 0;
  int channels = 2;
  std::size_t frameSize = 0;
  int samplesPerFrame = 0;
  int sideInfo = 0;
  bool crc = false;
};

int bitrateTable(int version, int layer, int index) {
  static constexpr int kMpeg1Layer1[15] = {
      0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448};
  static constexpr int kMpeg1Layer2[15] = {
      0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384};
  static constexpr int kMpeg1Layer3[15] = {0,   32,  40,  48,  56,  64,  80, 96,
                                           112, 128, 160, 192, 224, 256, 320};
  static constexpr int kMpeg2Layer1[15] = {
      0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256};
  static constexpr int kMpeg2Layer23[15] = {0,  8,  16, 24,  32,  40,  48, 56,
                                            64, 80, 96, 112, 128, 144, 160};
  if (index <= 0 || index >= 15)
    return 0;
  if (version == 1) {
    if (layer == 1)
      return kMpeg1Layer1[index];
    if (layer == 2)
      return kMpeg1Layer2[index];
    return kMpeg1Layer3[index];
  }
  if (layer == 1)
    return kMpeg2Layer1[index];
  return kMpeg2Layer23[index];
}

int sampleRateTable(int version, int index) {
  static constexpr int kMpeg1[3] = {44100, 48000, 32000};
  static constexpr int kMpeg2[3] = {22050, 24000, 16000};
  static constexpr int kMpeg25[3] = {11025, 12000, 8000};
  if (index < 0 || index > 2)
    return 0;
  if (version == 1)
    return kMpeg1[index];
  if (version == 2)
    return kMpeg2[index];
  return kMpeg25[index];
}

bool parseHeader(const std::uint8_t *p, Header &out) {
  if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0)
    return false;
  const int versionBits = (p[1] >> 3) & 0x3;
  const int layerBits = (p[1] >> 1) & 0x3;
  if (versionBits == 1 || layerBits == 0)
    return false; // reserved version / layer
  out.version = versionBits == 3 ? 1 : (versionBits == 2 ? 2 : 25);
  out.layer = layerBits == 3 ? 1 : (layerBits == 2 ? 2 : 3);
  const int bitrateIndex = (p[2] >> 4) & 0xF;
  const int sampleIndex = (p[2] >> 2) & 0x3;
  out.padding = (p[2] >> 1) & 0x1;
  out.channels = ((p[3] >> 6) & 0x3) == 3 ? 1 : 2;
  out.crc = (p[1] & 0x1) == 0;
  out.bitrateKbps = bitrateTable(out.version, out.layer, bitrateIndex);
  out.sampleRate = sampleRateTable(out.version, sampleIndex);
  if (out.bitrateKbps == 0 || out.sampleRate == 0)
    return false; // free format or bad index
  const int bitrate = out.bitrateKbps * 1000;
  if (out.layer == 1) {
    out.samplesPerFrame = 384;
    out.frameSize = static_cast<std::size_t>(
        (12 * bitrate / out.sampleRate + out.padding) * 4);
    out.sideInfo = 0;
  } else if (out.layer == 2) {
    out.samplesPerFrame = 1152;
    out.frameSize =
        static_cast<std::size_t>(144 * bitrate / out.sampleRate + out.padding);
    out.sideInfo = 0;
  } else {
    out.samplesPerFrame = out.version == 1 ? 1152 : 576;
    const int coefficient = out.version == 1 ? 144 : 72;
    out.frameSize = static_cast<std::size_t>(
        coefficient * bitrate / out.sampleRate + out.padding);
    out.sideInfo = out.version == 1 ? (out.channels == 1 ? 17 : 32)
                                    : (out.channels == 1 ? 9 : 17);
  }
  return out.frameSize >= 4;
}

std::uint32_t readBig32(const std::uint8_t *p) {
  return (static_cast<std::uint32_t>(p[0]) << 24) |
         (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) |
         static_cast<std::uint32_t>(p[3]);
}

std::size_t skipId3v2(const std::uint8_t *data, std::size_t size) {
  if (size < 10 || std::memcmp(data, "ID3", 3) != 0)
    return 0;
  const std::uint32_t tagSize =
      (static_cast<std::uint32_t>(data[6] & 0x7F) << 21) |
      (static_cast<std::uint32_t>(data[7] & 0x7F) << 14) |
      (static_cast<std::uint32_t>(data[8] & 0x7F) << 7) |
      static_cast<std::uint32_t>(data[9] & 0x7F);
  std::size_t offset = 10u + tagSize;
  if ((data[5] & 0x10) != 0) // footer present
    offset += 10;
  return offset < size ? offset : size;
}

} // namespace

double mp3DurationSeconds(const std::uint8_t *data, std::size_t size) {
  if (!data || size < 4)
    return 0;
  std::size_t offset = skipId3v2(data, size);
  const std::size_t searchEnd =
      offset + kMaxTagSkip < size ? offset + kMaxTagSkip : size;
  Header first;
  bool found = false;
  while (offset + 4 <= searchEnd) {
    if (parseHeader(data + offset, first)) {
      found = true;
      break;
    }
    ++offset;
  }
  if (!found)
    return 0;

  // Xing/Info VBR header: a frame count beats summing every frame.
  const std::size_t xingOffset = offset + 4 + (first.crc ? 2u : 0u) +
                                 static_cast<std::size_t>(first.sideInfo);
  if (xingOffset + 12 <= size &&
      (std::memcmp(data + xingOffset, "Xing", 4) == 0 ||
       std::memcmp(data + xingOffset, "Info", 4) == 0)) {
    const std::uint32_t flags = readBig32(data + xingOffset + 4);
    if ((flags & 0x1) != 0) {
      const std::uint32_t frames = readBig32(data + xingOffset + 8);
      if (frames > 0) {
        const double seconds = static_cast<double>(frames) *
                               first.samplesPerFrame / first.sampleRate;
        if (seconds > 0.1 && seconds < 60.0 * 60.0 * 24.0)
          return seconds;
      }
    }
  }

  std::uint64_t samples = 0;
  std::size_t frames = 0;
  while (offset + 4 <= size && frames < kMaxFrames) {
    Header header;
    if (!parseHeader(data + offset, header) ||
        header.sampleRate != first.sampleRate) {
      // One bad byte (encoders leave small gaps); resync on the next frame.
      std::size_t probe = offset + 1;
      const std::size_t probeEnd = offset + 4096 < size ? offset + 4096 : size;
      while (probe + 4 <= probeEnd && !parseHeader(data + probe, header))
        ++probe;
      if (probe + 4 > probeEnd)
        break;
      offset = probe;
      if (header.sampleRate != first.sampleRate)
        break;
    }
    if (header.frameSize > size - offset)
      break;
    samples += static_cast<std::uint64_t>(header.samplesPerFrame);
    offset += header.frameSize;
    ++frames;
  }
  if (samples == 0)
    return 0;
  const double seconds = static_cast<double>(samples) / first.sampleRate;
  return seconds > 0.1 ? seconds : 0;
}

} // namespace mediaplayer
