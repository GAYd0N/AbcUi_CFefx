#include "ApiAudio.h"

#include "ModuleResources.h"
#include "ModuleVfs.h"
#include "services/media/MemoryInput.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace framework::AudioApi {
namespace {
// SDL2 ABI, not SDL3: the game may implement SDL2 as a compatibility DLL.
struct AudioSpec {
  int frequency;
  uint16_t format;
  uint8_t channels, silence;
  uint16_t samples, padding;
  uint32_t size;
  void(__cdecl *callback)(void *, uint8_t *, int);
  void *userdata;
};
static_assert(sizeof(AudioSpec) == 24);
constexpr uint32_t kAudioSubsystem = 0x10;
constexpr int kFrequency = 44100;
constexpr int kChannels = 2;
constexpr uint32_t kMaxQueuedBytes = kFrequency * kChannels * 2 / 4;
constexpr size_t kMaxReadyChunks = 8;

struct SdlAudio {
  int(__cdecl *init)(uint32_t) = nullptr;
  void(__cdecl *quit)(uint32_t) = nullptr;
  uint32_t(__cdecl *open)(const char *, int, const AudioSpec *, AudioSpec *,
                          int) = nullptr;
  void(__cdecl *close)(uint32_t) = nullptr;
  void(__cdecl *pause)(uint32_t, int) = nullptr;
  int(__cdecl *queue)(uint32_t, const void *, uint32_t) = nullptr;
  uint32_t(__cdecl *queued)(uint32_t) = nullptr;

  bool bind() {
    // Only use the game's loaded SDL2. Never introduce a second SDL runtime
    // or call SDL_Quit on the engine's subsystem references.
    const auto dll = GetModuleHandleA("SDL2.dll");
    if (!dll)
      return false;
#define ABCUI_BIND_SDL(member, name)                                           \
  member = reinterpret_cast<decltype(member)>(GetProcAddress(dll, name));      \
  if (!member)                                                                 \
    return false;
    ABCUI_BIND_SDL(init, "SDL_InitSubSystem")
    ABCUI_BIND_SDL(quit, "SDL_QuitSubSystem")
    ABCUI_BIND_SDL(open, "SDL_OpenAudioDevice")
    ABCUI_BIND_SDL(close, "SDL_CloseAudioDevice")
    ABCUI_BIND_SDL(pause, "SDL_PauseAudioDevice")
    ABCUI_BIND_SDL(queue, "SDL_QueueAudio")
    ABCUI_BIND_SDL(queued, "SDL_GetQueuedAudioSize")
#undef ABCUI_BIND_SDL
    return true;
  }
};

struct Stream {
  module_resources::Registration lifetime;
  SdlAudio sdl;
  uint32_t device = 0;
  bool initialized = false;
  bool paused = true;
  bool loop = false;
  float volume = 1;
  std::atomic<int32_t> result{ABCUI_ERROR_PENDING};
  std::atomic<bool> stopping{false};
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<std::vector<int16_t>> ready;
  std::vector<int16_t> pending;
  size_t pendingOffset = 0;
  std::thread worker;
  media::MemoryInput input;

  ~Stream() {
    stopping = true;
    wake.notify_all();
    if (worker.joinable())
      worker.join();
    if (device)
      sdl.close(device);
    if (initialized)
      sdl.quit(kAudioSubsystem);
  }

  bool startDevice() {
    if (!sdl.bind() || sdl.init(kAudioSubsystem) != 0)
      return false;
    initialized = true;
    AudioSpec desired{};
    desired.frequency = kFrequency;
    desired.format = 0x8010; // AUDIO_S16LSB
    desired.channels = kChannels;
    desired.samples = 1024;
    AudioSpec actual{};
    device = sdl.open(nullptr, 0, &desired, &actual, 0);
    return device != 0;
  }

  bool push(std::vector<int16_t> chunk) {
    if (chunk.empty())
      return true;
    std::unique_lock lock(mutex);
    wake.wait(lock, [&] { return stopping || ready.size() < kMaxReadyChunks; });
    if (stopping)
      return false;
    ready.push_back(std::move(chunk));
    result = ABCUI_OK;
    return true;
  }

  void decode() {
    const auto &api = media::ffmpeg();
    AVIOContext *io = input.create(api);
    AVFormatContext *format = api.avformat_alloc_context();
    AVCodecContext *codec = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    SwrContext *resampler = nullptr;
    const auto cleanup = [&] {
      api.swr_free(&resampler);
      api.av_packet_free(&packet);
      api.av_frame_free(&frame);
      api.avcodec_free_context(&codec);
      api.avformat_close_input(&format);
      media::MemoryInput::destroy(api, io);
    };
    struct Guard {
      decltype(cleanup) &fn;
      ~Guard() {
        fn();
      }
    } guard{cleanup};
    if (!io || !format) {
      result = ABCUI_ERROR_UNAVAILABLE;
      return;
    }
    if (!media::MemoryInput::configure(format, api, "mp3,wav,ogg,flac,aac")) {
      result = ABCUI_ERROR_UNAVAILABLE;
      return;
    }
    format->pb = io;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
    int code = api.avformat_open_input(&format, nullptr, nullptr, nullptr);
    int stream = -1;
    const AVCodec *decoder = nullptr;
    if (code >= 0)
      code = api.avformat_find_stream_info(format, nullptr);
    if (code >= 0)
      stream = api.av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1,
                                       &decoder, 0);
    if (stream >= 0) {
      codec = api.avcodec_alloc_context3(decoder);
      if (codec)
        code = api.avcodec_parameters_to_context(
            codec, format->streams[stream]->codecpar);
      else
        code = AVERROR(ENOMEM);
      if (code >= 0)
        code = api.avcodec_open2(codec, decoder, nullptr);
    }
    if (code < 0 || stream < 0 || !codec || codec->sample_rate <= 0 ||
        codec->ch_layout.nb_channels <= 0) {
      result = ABCUI_ERROR_DECODE;
      return;
    }
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    const auto makeResampler = [&] {
      api.swr_free(&resampler);
      return api.swr_alloc_set_opts2(&resampler, &stereo, AV_SAMPLE_FMT_S16,
                                     kFrequency, &codec->ch_layout,
                                     codec->sample_fmt, codec->sample_rate, 0,
                                     nullptr) >= 0 &&
             api.swr_init(resampler) >= 0;
    };
    frame = api.av_frame_alloc();
    packet = api.av_packet_alloc();
    if (!frame || !packet || !makeResampler()) {
      result = ABCUI_ERROR_DECODE;
      return;
    }
    bool cycleHasSamples = false;
    const auto convert = [&](const uint8_t **samples, int count) {
      const int capacity = api.swr_get_out_samples(resampler, count);
      if (capacity < 0 || capacity > 65536)
        return false;
      std::vector<int16_t> pcm(static_cast<size_t>(capacity) * kChannels);
      uint8_t *output = reinterpret_cast<uint8_t *>(pcm.data());
      const int frames =
          api.swr_convert(resampler, &output, capacity, samples, count);
      if (frames < 0)
        return false;
      pcm.resize(static_cast<size_t>(frames) * kChannels);
      cycleHasSamples = cycleHasSamples || frames > 0;
      return push(std::move(pcm));
    };
    const auto drain = [&] {
      int received = 0;
      while (!stopping &&
             (received = api.avcodec_receive_frame(codec, frame)) >= 0) {
        if (frame->sample_rate != codec->sample_rate ||
            frame->format != codec->sample_fmt ||
            frame->ch_layout.nb_channels != codec->ch_layout.nb_channels ||
            !convert(const_cast<const uint8_t **>(frame->extended_data),
                     frame->nb_samples))
          return false;
        api.av_frame_unref(frame);
      }
      return received == AVERROR(EAGAIN) || received == AVERROR_EOF || stopping;
    };
    while (!stopping) {
      code = api.av_read_frame(format, packet);
      if (code < 0) {
        if (code != AVERROR_EOF ||
            api.avcodec_send_packet(codec, nullptr) < 0 || !drain() ||
            !convert(nullptr, 0)) {
          if (!stopping)
            result = ABCUI_ERROR_DECODE;
          break;
        }
        if (!cycleHasSamples) {
          result = ABCUI_ERROR_DECODE;
          break;
        }
        if (!loop)
          break;
        cycleHasSamples = false;
        if (api.av_seek_frame(format, stream, 0, AVSEEK_FLAG_BACKWARD) < 0 ||
            !makeResampler()) {
          result = ABCUI_ERROR_DECODE;
          break;
        }
        api.avcodec_flush_buffers(codec);
        continue;
      }
      if (packet->stream_index == stream) {
        code = api.avcodec_send_packet(codec, packet);
        if (code == AVERROR(EAGAIN) && drain())
          code = api.avcodec_send_packet(codec, packet);
        if (code < 0 || !drain()) {
          if (!stopping)
            result = ABCUI_ERROR_DECODE;
          break;
        }
      }
      api.av_packet_unref(packet);
    }
  }

  void pump() {
    if (paused || result < 0 || !device)
      return;
    uint32_t queuedBytes = sdl.queued(device);
    while (queuedBytes < kMaxQueuedBytes) {
      if (pendingOffset == pending.size()) {
        std::lock_guard lock(mutex);
        if (ready.empty())
          break;
        pending = std::move(ready.front());
        pendingOffset = 0;
        ready.pop_front();
        wake.notify_one();
      }
      size_t capacity = (kMaxQueuedBytes - queuedBytes) / sizeof(int16_t);
      capacity -= capacity % kChannels;
      const auto count = std::min(capacity, pending.size() - pendingOffset);
      if (count == 0)
        break;
      std::vector<int16_t> pcm(pending.begin() + pendingOffset,
                               pending.begin() + pendingOffset + count);
      pendingOffset += count;
      if (volume < 1)
        for (auto &sample : pcm)
          sample = static_cast<int16_t>(sample * volume);
      if (sdl.queue(device, pcm.data(),
                    static_cast<uint32_t>(pcm.size() * sizeof(int16_t))) != 0) {
        result = ABCUI_ERROR_UNAVAILABLE;
        stopping = true;
        wake.notify_all();
        break;
      }
      queuedBytes = sdl.queued(device);
    }
    sdl.pause(device, 0);
  }
};

std::map<AbcUiAudio, std::unique_ptr<Stream>> &streams() {
  static std::map<AbcUiAudio, std::unique_ptr<Stream>> instance;
  return instance;
}

Stream *owned(AbcUiAudio handle) {
  const auto it = streams().find(handle);
  return it != streams().end() &&
                 it->second->lifetime.owner() == module_resources::current()
             ? it->second.get()
             : nullptr;
}

int32_t open(const char *uri, int32_t loop, AbcUiAudio *out) {
  if (out)
    *out = 0;
  if (!uri || !out)
    return ABCUI_ERROR_INVALID;
  const auto owner = module_resources::current();
  if (!owner || !module_resources::accepting() ||
      !module_resources::contextRegistrationsOpen() ||
      !ModuleVfs::canRead(owner->id, uri))
    return ABCUI_ERROR_FORBIDDEN;
  if (streams().size() >= 16)
    return ABCUI_ERROR_LIMIT;
  auto entry = std::make_shared<PackageEntry>();
  if (!ModuleVfs::open(uri, *entry, nullptr))
    return ABCUI_ERROR_NOT_FOUND;
  if (entry->size == 0 || entry->size > 128 * 1024 * 1024)
    return ABCUI_ERROR_LIMIT;
  if (!media::available())
    return ABCUI_ERROR_UNAVAILABLE;
  auto stream = std::make_unique<Stream>();
  stream->input.entry = std::move(entry);
  stream->loop = loop != 0;
  if (!stream->startDevice())
    return ABCUI_ERROR_UNAVAILABLE;
  static AbcUiAudio next = 1;
  const auto handle = next++;
  auto *raw = stream.get();
  streams().emplace(handle, std::move(stream));
  raw->lifetime.track([handle] { streams().erase(handle); },
                      module_resources::Lifetime::Context);
  raw->worker = std::thread([raw] {
    try {
      raw->decode();
    } catch (...) {
      raw->result = ABCUI_ERROR_UNAVAILABLE;
    }
  });
  *out = handle;
  return ABCUI_OK;
}

int32_t status(AbcUiAudio handle) {
  const auto *stream = owned(handle);
  return stream ? stream->result.load() : ABCUI_ERROR_INVALID;
}
void setPaused(AbcUiAudio handle, int32_t paused) {
  if (auto *stream = owned(handle)) {
    stream->paused = paused != 0;
    stream->sdl.pause(stream->device, paused != 0);
  }
}
void setVolume(AbcUiAudio handle, float volume) {
  if (auto *stream = owned(handle))
    stream->volume = std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f) : 0;
}
void close(AbcUiAudio handle) {
  if (owned(handle))
    streams().erase(handle);
}
} // namespace

void pump() {
  for (auto &[handle, stream] : streams())
    stream->pump();
}

const AbcUiAudioApi *get() {
  static const AbcUiAudioApi api{sizeof(api), open,      status,
                                 setPaused,   setVolume, close};
  return &api;
}
} // namespace framework::AudioApi
