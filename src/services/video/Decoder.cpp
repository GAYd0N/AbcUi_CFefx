// services::video::Decoder implementation - see Decoder.h.
#include "Decoder.h"
#include "services/media/MemoryInput.h"
#include <mutex>

#include <windows.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace video {

namespace {

constexpr int kMaxOutputEdge = 2048;
constexpr int kDecodeThreads = 4;
// Bounds one advance() call: a stalled or very long frame gap must never turn
// into an unbounded decode burst inside a UI frame.
constexpr int kMaxFramesPerAdvance = 16;
constexpr double kFrameSlackSeconds = 1.0 / 1000.0;

using FfmpegApi = media::FfmpegApi;
const FfmpegApi &ffmpegApi() {
  return media::ffmpeg();
}

double rationalSeconds(int64_t value, AVRational base) {
  if (value == AV_NOPTS_VALUE || base.num <= 0 || base.den <= 0)
    return -1.0;
  return static_cast<double>(value) * base.num / base.den;
}

int64_t secondsToTimestamp(double seconds, AVRational base) {
  if (base.num <= 0 || base.den <= 0)
    return 0;
  const double ticks = seconds * base.den / base.num;
  return static_cast<int64_t>(ticks);
}

} // namespace

struct Decoder::State {
  AVFormatContext *Format = nullptr;
  AVIOContext *Io = nullptr;
  media::MemoryInput Input;
  AVCodecContext *Codec = nullptr;
  AVFrame *Frame = nullptr;
  AVPacket *Packet = nullptr;
  int StreamIndex = -1;
  SwsContext *Sws = nullptr;
  // Dimensions/format the current Sws was created for; a mid-stream
  // resolution change must rebuild it (the old context would read the new
  // plane layout with stale strides).
  int SwsSourceWidth = 0;
  int SwsSourceHeight = 0;
  int SwsSourceFormat = -1;
  int OutputWidth = 0;
  int OutputHeight = 0;
  std::vector<unsigned char> Output;
  FrameView View;

  // Decode pipeline state.
  bool Draining = false;
  bool Eof = false;
  bool PacketPending = false;
  bool FramePending = false; // Frame holds a decoded frame not yet shown
  double PendingSeconds = -1.0;
  double LastSeconds = -1.0; // presentation time of the shown frame
  double Clock = 0.0;
  double Duration = -1.0;
  bool Looping = true;
  int64_t FrameCounter = 0;
  std::string Error;
  FfmpegApi Api{};

  bool fail(const char *operation, int result = 0) {
    Error = operation;
    if (result && Api.av_strerror) {
      char text[AV_ERROR_MAX_STRING_SIZE] = {};
      Api.av_strerror(result, text, sizeof(text));
      Error += ": ";
      Error += text;
    }
    return false;
  }

  void resetPipeline() {
    Eof = false;
    Draining = false;
    PacketPending = false;
    FramePending = false;
    PendingSeconds = -1.0;
    Clock = 0.0;
  }

  // Pulls the next decoded frame into Frame. Returns 1 = frame ready,
  // 0 = end of stream, -1 = error (Error set).
  int nextFrame() {
    for (;;) {
      if (FramePending)
        return 1;
      const int receive = Api.avcodec_receive_frame(Codec, Frame);
      if (receive == 0) {
        FramePending = true;
        PendingSeconds =
            rationalSeconds(Frame->best_effort_timestamp != AV_NOPTS_VALUE
                                ? Frame->best_effort_timestamp
                                : Frame->pts,
                            Format->streams[StreamIndex]->time_base);
        if (PendingSeconds < 0.0)
          PendingSeconds = static_cast<double>(FrameCounter) / 30.0;
        FrameCounter++;
        return 1;
      }
      if (receive == AVERROR_EOF)
        return 0;
      if (receive != AVERROR(EAGAIN))
        return -1;
      if (Draining)
        return 0;
      // The decoder wants more input: feed the pending packet, then read
      // another one.
      if (PacketPending) {
        const int send = Api.avcodec_send_packet(Codec, Packet);
        Api.av_packet_unref(Packet);
        PacketPending = false;
        if (send < 0 && send != AVERROR(EAGAIN))
          return -1;
        continue;
      }
      const int read = Api.av_read_frame(Format, Packet);
      if (read == AVERROR_EOF) {
        Draining = true;
        Api.avcodec_send_packet(Codec, nullptr);
        continue;
      }
      if (read < 0)
        return -1;
      if (Packet->stream_index != StreamIndex) {
        Api.av_packet_unref(Packet);
        continue;
      }
      PacketPending = true;
    }
  }

  // Converts Frame into the RGBA output buffer. Returns false on error.
  bool convertPending() {
    if (Sws &&
        (SwsSourceWidth != Frame->width || SwsSourceHeight != Frame->height ||
         SwsSourceFormat != Frame->format)) {
      Api.sws_freeContext(Sws);
      Sws = nullptr;
    }
    if (!Sws) {
      Sws = Api.sws_getContext(Frame->width, Frame->height,
                               static_cast<AVPixelFormat>(Frame->format),
                               OutputWidth, OutputHeight, AV_PIX_FMT_RGBA,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
      if (!Sws)
        return fail("cannot create the video scaler");
      SwsSourceWidth = Frame->width;
      SwsSourceHeight = Frame->height;
      SwsSourceFormat = Frame->format;
    }
    unsigned char *destination[4] = {Output.data(), nullptr, nullptr, nullptr};
    int strides[4] = {OutputWidth * 4, 0, 0, 0};
    const int rows = Api.sws_scale(Sws, Frame->data, Frame->linesize, 0,
                                   Frame->height, destination, strides);
    if (rows <= 0)
      return fail("video scaling failed");
    View.Pixels = Output.data();
    View.Width = OutputWidth;
    View.Height = OutputHeight;
    LastSeconds = PendingSeconds;
    FramePending = false;
    return true;
  }

  void seekToStart() {
    if (!Format || StreamIndex < 0)
      return;
    if (Api.av_seek_frame(Format, StreamIndex, 0, AVSEEK_FLAG_BACKWARD) < 0)
      return; // keep decoding forward if the container cannot seek
    Api.avcodec_flush_buffers(Codec);
    Api.av_packet_unref(Packet);
    Api.av_frame_unref(Frame);
    resetPipeline();
    FrameCounter = 0;
    LastSeconds = -1.0;
  }
};

Decoder::Decoder() : Data(std::make_unique<State>()) {}

Decoder::~Decoder() {
  close();
}

bool Decoder::available() {
  const FfmpegApi &api = ffmpegApi();
  if (!api.avcodec_version)
    return false;
  return AV_VERSION_MAJOR(api.avcodec_version()) == LIBAVCODEC_VERSION_MAJOR &&
         AV_VERSION_MAJOR(api.avformat_version()) ==
             LIBAVFORMAT_VERSION_MAJOR &&
         AV_VERSION_MAJOR(api.avutil_version()) == LIBAVUTIL_VERSION_MAJOR;
}

void Decoder::close() {
  if (!Data)
    return;
  State &state = *Data;
  if (state.Sws) {
    state.Api.sws_freeContext(state.Sws);
    state.Sws = nullptr;
  }
  if (state.Packet) {
    state.Api.av_packet_free(&state.Packet);
    state.Packet = nullptr;
  }
  if (state.Frame) {
    state.Api.av_frame_free(&state.Frame);
    state.Frame = nullptr;
  }
  if (state.Codec) {
    state.Api.avcodec_free_context(&state.Codec);
    state.Codec = nullptr;
  }
  if (state.Format) {
    state.Api.avformat_close_input(&state.Format);
    state.Format = nullptr;
  }
  media::MemoryInput::destroy(state.Api, state.Io);
  state.Input = {};
  state.StreamIndex = -1;
  state.Output.clear();
  state.View = {};
  state.Api = {};
  state.Error.clear();
  state.resetPipeline();
}

bool Decoder::open(std::shared_ptr<const framework::PackageEntry> source,
                   int outputWidth, int outputHeight) {
  close();
  if (!source || !source->data || !source->size)
    return false;
  State &state = *Data;
  state.Api = ffmpegApi();
  if (!state.Api.avcodec_version)
    return state.fail(
        "Mile.FFmpeg.dll unavailable (install metahook/dlls/Mile.FFmpeg)");
  if (!available())
    return state.fail("incompatible Mile.FFmpeg ABI (FFmpeg 6.x required)");
  if (outputWidth <= 0 || outputHeight <= 0)
    return state.fail("invalid video output size");
  state.OutputWidth = std::clamp(outputWidth, 1, kMaxOutputEdge);
  state.OutputHeight = std::clamp(outputHeight, 1, kMaxOutputEdge);

  state.Input.entry = std::move(source);
  state.Io = state.Input.create(state.Api);
  state.Format = state.Api.avformat_alloc_context();
  if (!state.Io || !state.Format)
    return state.fail("cannot allocate video memory reader");
  if (!media::MemoryInput::configure(state.Format, state.Api,
                                     "mov,matroska,webm,avi,ivf,mpeg,mpegts"))
    return state.fail("cannot configure video memory reader");
  state.Format->pb = state.Io;
  state.Format->flags |= AVFMT_FLAG_CUSTOM_IO;
  int result =
      state.Api.avformat_open_input(&state.Format, nullptr, nullptr, nullptr);
  if (result < 0)
    return state.fail("cannot open video", result);
  result = state.Api.avformat_find_stream_info(state.Format, nullptr);
  if (result < 0)
    return state.fail("cannot read video tracks", result);
  state.StreamIndex = state.Api.av_find_best_stream(
      state.Format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (state.StreamIndex < 0)
    return state.fail("video has no video track", state.StreamIndex);

  AVStream *stream = state.Format->streams[state.StreamIndex];
  const AVCodec *codec =
      state.Api.avcodec_find_decoder(stream->codecpar->codec_id);
  if (!codec)
    return state.fail("Mile.FFmpeg lacks this video codec");
  state.Codec = state.Api.avcodec_alloc_context3(codec);
  if (!state.Codec)
    return state.fail("cannot allocate the video decoder");
  result =
      state.Api.avcodec_parameters_to_context(state.Codec, stream->codecpar);
  if (result < 0)
    return state.fail("invalid video parameters", result);
  state.Codec->thread_count = kDecodeThreads;
  result = state.Api.avcodec_open2(state.Codec, codec, nullptr);
  if (result < 0)
    return state.fail("cannot start the video decoder", result);
  if (state.Codec->width <= 0 || state.Codec->height <= 0)
    return state.fail("invalid video dimensions");

  state.Frame = state.Api.av_frame_alloc();
  state.Packet = state.Api.av_packet_alloc();
  if (!state.Frame || !state.Packet)
    return state.fail("cannot allocate video frame storage");
  state.Output.assign(
      static_cast<std::size_t>(state.OutputWidth) * state.OutputHeight * 4, 0);
  state.Duration = rationalSeconds(stream->duration, stream->time_base);
  if (state.Duration <= 0.0 && state.Format->duration > 0)
    state.Duration = static_cast<double>(state.Format->duration) / AV_TIME_BASE;
  state.resetPipeline();
  return true;
}

bool Decoder::isOpen() const {
  return Data && Data->Format != nullptr && Data->Codec != nullptr;
}

void Decoder::setLooping(bool looping) {
  if (Data)
    Data->Looping = looping;
}

bool Decoder::looping() const {
  return Data && Data->Looping;
}

bool Decoder::atEnd() const {
  return Data && Data->Eof;
}

int Decoder::sourceWidth() const {
  return Data && Data->Codec ? Data->Codec->width : 0;
}

int Decoder::sourceHeight() const {
  return Data && Data->Codec ? Data->Codec->height : 0;
}

double Decoder::duration() const {
  return Data ? Data->Duration : -1.0;
}

const FrameView &Decoder::frame() const {
  static const FrameView Empty;
  return Data ? Data->View : Empty;
}

const std::string &Decoder::error() const {
  static const std::string Empty;
  return Data ? Data->Error : Empty;
}

double Decoder::lastSeconds() const {
  return Data ? Data->LastSeconds : -1.0;
}

bool Decoder::advance(double deltaSeconds) {
  if (!isOpen())
    return false;
  State &state = *Data;
  if (state.Eof && !state.Looping)
    return false;
  if (!(deltaSeconds > 0.0))
    deltaSeconds = 0.0;
  if (deltaSeconds > 0.5)
    deltaSeconds = 0.5; // a hitching frame must not skip half the movie
  state.Clock += deltaSeconds;

  // Loop or stop at the end of the stream: the last frame stays on screen
  // until the clock is rewound (no black flash while the movie restarts).
  if (state.Eof && state.Looping && state.Duration > 0.0 &&
      state.Clock >= state.Duration) {
    const double overrun = state.Clock - state.Duration;
    state.seekToStart();
    state.Clock = std::fmod(overrun, state.Duration);
  }

  bool refreshed = false;
  for (int step = 0; step < kMaxFramesPerAdvance; step++) {
    if (!state.FramePending) {
      const int pulled = state.nextFrame();
      if (pulled == 0) {
        state.Eof = true;
        if (!state.Looping)
          break;
        if (state.Duration > 0.0 && state.Clock < state.Duration)
          state.Clock = state.Duration; // the loop check above fires next call
        break;
      }
      if (pulled < 0) {
        state.fail("video decode failed");
        break;
      }
    }
    if (state.PendingSeconds > state.Clock + kFrameSlackSeconds) {
      // The next frame is still in the future; the held frame becomes
      // current on a later advance().
      if (state.LastSeconds < 0.0) {
        // Always present something on the first tick, otherwise the
        // document's first frame shows nothing (and a video that starts
        // with a non-zero timestamp would never appear).
        state.PendingSeconds = 0.0;
        continue;
      }
      break;
    }
    if (!state.convertPending())
      break;
    refreshed = true;
  }
  return refreshed;
}

} // namespace video
