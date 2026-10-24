// Animated WebP backend - see WebPDecoder.h.
#include "WebPDecoder.h"

#include "../../framework/ResourceLimits.h"

#include <webp/demux.h>

#include <cstdint>
#include <utility>

namespace rmlui::animated::detail {

namespace {

// RIFF....WEBP (the bytes every animated or still WebP starts with).
bool probeWebP(std::span<const std::byte> data) {
  if (data.size() < 12)
    return false;
  return data[0] == std::byte{'R'} && data[1] == std::byte{'I'} &&
         data[2] == std::byte{'F'} && data[3] == std::byte{'F'} &&
         data[8] == std::byte{'W'} && data[9] == std::byte{'E'} &&
         data[10] == std::byte{'B'} && data[11] == std::byte{'P'};
}

class WebPAnimation final : public AnimatedImage {
public:
  ~WebPAnimation() override {
    if (Decoder)
      WebPAnimDecoderDelete(Decoder);
  }

  bool open(std::vector<std::byte> bytes, std::string source,
            std::string &error) {
    Bytes = std::move(bytes);
    SourceName = std::move(source);
    FormatName = "webp";
    // The decoder borrows this buffer for its whole lifetime.
    const WebPData data = {reinterpret_cast<const uint8_t *>(Bytes.data()),
                           Bytes.size()};

    // Metadata pass: durations come from the container, no pixels decode.
    WebPDemuxer *demux = WebPDemux(&data);
    if (!demux) {
      error = "webp container rejected";
      return false;
    }
    const uint32_t declaredWidth = WebPDemuxGetI(demux, WEBP_FF_CANVAS_WIDTH);
    const uint32_t declaredHeight = WebPDemuxGetI(demux, WEBP_FF_CANVAS_HEIGHT);
    WebPIterator iter;
    if (WebPDemuxGetFrame(demux, 1, &iter)) {
      do {
        Durations.push_back(iter.duration);
      } while (WebPDemuxNextFrame(&iter));
      WebPDemuxReleaseIterator(&iter);
    }
    WebPDemuxDelete(demux);

    std::size_t canvasBytes = 0;
    if (!framework::ResourceLimits::imageBytes(static_cast<int>(declaredWidth),
                                               static_cast<int>(declaredHeight),
                                               canvasBytes)) {
      error = "webp canvas out of range";
      return false;
    }

    WebPAnimDecoderOptions options;
    if (!WebPAnimDecoderOptionsInit(&options)) {
      error = "webp decoder options unavailable";
      return false;
    }
    options.color_mode = MODE_RGBA;
    options.use_threads = 0;
    Decoder = WebPAnimDecoderNew(&data, &options);
    WebPAnimInfo info = {};
    if (!Decoder || !WebPAnimDecoderGetInfo(Decoder, &info)) {
      error = "webp animation decoder rejected";
      return false;
    }
    PixelWidth = static_cast<int>(info.canvas_width);
    PixelHeight = static_cast<int>(info.canvas_height);
    FrameTotal = static_cast<int>(info.frame_count);
    if (!framework::ResourceLimits::imageBytes(PixelWidth, PixelHeight,
                                               canvasBytes) ||
        FrameTotal <= 0) {
      error = "webp animation has no usable frames";
      return false;
    }
    return true;
  }

  bool Rewind() override {
    if (!Decoder)
      return false;
    WebPAnimDecoderReset(Decoder);
    NextIndex = 0;
    return true;
  }

  bool Read(FrameView &out) override {
    if (!Decoder || NextIndex >= FrameTotal)
      return false;
    uint8_t *buffer = nullptr;
    int timestamp = 0;
    if (!WebPAnimDecoderGetNext(Decoder, &buffer, &timestamp) || !buffer)
      return false;
    out.Pixels = buffer;
    out.Width = PixelWidth;
    out.Height = PixelHeight;
    out.DurationMs = DurationMs(NextIndex);
    out.Index = NextIndex;
    NextIndex++;
    return true;
  }

private:
  WebPAnimDecoder *Decoder = nullptr;
  int NextIndex = 0;
};

std::unique_ptr<AnimatedImage>
openWebP(std::vector<std::byte> bytes, std::string source, std::string &error) {
  auto animation = std::make_unique<WebPAnimation>();
  if (!animation->open(std::move(bytes), std::move(source), error))
    return nullptr;
  return animation;
}

} // namespace

const Backend &webpBackend() {
  static const Backend backend{"webp", probeWebP, openWebP};
  return backend;
}

} // namespace rmlui::animated::detail
