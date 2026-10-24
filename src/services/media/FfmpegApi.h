#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#define ABCUI_FFMPEG_APIS(X)                                                   \
  X(avcodec_version)                                                           \
  X(avformat_version)                                                          \
  X(avutil_version)                                                            \
  X(swresample_version)                                                        \
  X(av_strerror)                                                               \
  X(av_malloc)                                                                 \
  X(av_strdup)                                                                 \
  X(av_freep)                                                                  \
  X(avio_alloc_context)                                                        \
  X(avio_context_free)                                                         \
  X(avformat_alloc_context)                                                    \
  X(avformat_find_stream_info)                                                 \
  X(avcodec_free_context)                                                      \
  X(avformat_open_input)                                                       \
  X(avformat_close_input)                                                      \
  X(av_find_best_stream)                                                       \
  X(av_read_frame)                                                             \
  X(avcodec_alloc_context3)                                                    \
  X(avcodec_parameters_to_context)                                             \
  X(avcodec_find_decoder)                                                      \
  X(avcodec_open2)                                                             \
  X(avcodec_send_packet)                                                       \
  X(avcodec_receive_frame)                                                     \
  X(avcodec_flush_buffers)                                                     \
  X(av_packet_alloc)                                                           \
  X(av_packet_free)                                                            \
  X(av_packet_unref)                                                           \
  X(av_frame_alloc)                                                            \
  X(av_frame_free)                                                             \
  X(av_frame_unref)                                                            \
  X(av_seek_frame)                                                             \
  X(sws_getContext)                                                            \
  X(sws_freeContext)                                                           \
  X(sws_scale)                                                                 \
  X(swr_alloc_set_opts2)                                                       \
  X(swr_init)                                                                  \
  X(swr_convert)                                                               \
  X(swr_free)                                                                  \
  X(swr_get_out_samples)

namespace media {
struct FfmpegApi {
#define ABCUI_DECLARE_FFMPEG(name) decltype(&::name) name{};
  ABCUI_FFMPEG_APIS(ABCUI_DECLARE_FFMPEG)
#undef ABCUI_DECLARE_FFMPEG
};
// One process binding, verified against the pinned SDK ABI before use.
const FfmpegApi &ffmpeg();
bool available();
} // namespace media
