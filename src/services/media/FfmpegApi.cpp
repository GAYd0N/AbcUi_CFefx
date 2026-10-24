#include "FfmpegApi.h"

#include <windows.h>

#include <mutex>

namespace media {
const FfmpegApi &ffmpeg() {
  static FfmpegApi api;
  static std::once_flag once;
  std::call_once(once, [] {
    const auto library = LoadLibraryA("Mile.FFmpeg.dll");
    if (!library)
      return;
    bool complete = true;
#define ABCUI_LOAD_FFMPEG(name)                                                \
  api.name =                                                                   \
      reinterpret_cast<decltype(api.name)>(GetProcAddress(library, #name));    \
  complete = complete && api.name != nullptr;
    ABCUI_FFMPEG_APIS(ABCUI_LOAD_FFMPEG)
#undef ABCUI_LOAD_FFMPEG
    if (!complete ||
        AV_VERSION_MAJOR(api.avcodec_version()) != LIBAVCODEC_VERSION_MAJOR ||
        AV_VERSION_MAJOR(api.avformat_version()) != LIBAVFORMAT_VERSION_MAJOR ||
        AV_VERSION_MAJOR(api.avutil_version()) != LIBAVUTIL_VERSION_MAJOR ||
        AV_VERSION_MAJOR(api.swresample_version()) !=
            LIBSWRESAMPLE_VERSION_MAJOR) {
      api = {};
      FreeLibrary(library);
    }
    // A successful binding stays resident until process exit.
  });
  return api;
}

bool available() {
  return ffmpeg().avcodec_version != nullptr;
}
} // namespace media
