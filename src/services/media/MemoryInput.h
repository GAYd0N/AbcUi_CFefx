#pragma once

#include "FfmpegApi.h"
#include "modsys/PackageFile.h"

#include <algorithm>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>

namespace media {
// The caller acquires the package on the main thread. Workers use only this
// pinned immutable view; no SDK/VFS/engine calls occur on decode threads.
struct MemoryInput {
  std::shared_ptr<const framework::PackageEntry> entry;
  int64_t cursor = 0;

  static int read(void *opaque, uint8_t *buffer, int bytes) {
    auto &input = *static_cast<MemoryInput *>(opaque);
    if (!input.entry || bytes <= 0)
      return AVERROR(EINVAL);
    const auto remaining =
        static_cast<int64_t>(input.entry->size) - input.cursor;
    if (remaining <= 0)
      return AVERROR_EOF;
    const int count = static_cast<int>(std::min<int64_t>(remaining, bytes));
    std::memcpy(buffer,
                static_cast<const uint8_t *>(input.entry->data) + input.cursor,
                count);
    input.cursor += count;
    return count;
  }

  static int64_t seek(void *opaque, int64_t offset, int origin) {
    auto &input = *static_cast<MemoryInput *>(opaque);
    if (!input.entry)
      return AVERROR(EINVAL);
    const auto size = static_cast<int64_t>(input.entry->size);
    origin &= ~AVSEEK_FORCE;
    if (origin == AVSEEK_SIZE)
      return size;
    const int64_t base = origin == SEEK_SET   ? 0
                         : origin == SEEK_CUR ? input.cursor
                         : origin == SEEK_END ? size
                                              : -1;
    if (base < 0 || offset < -base || offset > size - base)
      return AVERROR(EINVAL);
    return input.cursor = base + offset;
  }

  AVIOContext *create(const FfmpegApi &api) {
    auto *buffer = static_cast<unsigned char *>(api.av_malloc(32768));
    if (!buffer)
      return nullptr;
    auto *io =
        api.avio_alloc_context(buffer, 32768, 0, this, read, nullptr, seek);
    if (!io)
      api.av_freep(&buffer);
    return io;
  }

  static bool configure(AVFormatContext *format, const FfmpegApi &api,
                        const char *formats) {
    // A demuxer must never follow a playlist, sidecar or external data
    // reference outside the pinned package view, even when supplied a
    // misleading suffix.
    format->format_whitelist = api.av_strdup(formats);
    format->protocol_whitelist = api.av_strdup("abcui-memory-only");
    format->io_open = [](AVFormatContext *, AVIOContext **, const char *, int,
                         AVDictionary **) { return AVERROR(EACCES); };
    return format->format_whitelist && format->protocol_whitelist;
  }

  static void destroy(const FfmpegApi &api, AVIOContext *&io) {
    if (io) {
      api.av_freep(&io->buffer);
      api.avio_context_free(&io);
    }
  }
};
} // namespace media
