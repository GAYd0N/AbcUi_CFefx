#include "FfmpegApi.h"
#include "framework/NativeHandles.h"

#include <windows.h>

namespace media {
const FfmpegApi &ffmpeg() {
  struct Binding {
    framework::NativeLibrary library;
    FfmpegApi api;
  };
  static Binding binding = [] {
    Binding result;
    result.library.reset(LoadLibraryA("Mile.FFmpeg.dll"));
    const auto library = result.library.get();
    if (!library)
      return result;
    auto &api = result.api;
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
      return Binding{};
    }
    // Workers stop during plugin shutdown, before this owner releases the DLL.
    return result;
  }();
  return binding.api;
}

bool available() {
  return ffmpeg().avcodec_version != nullptr;
}
} // namespace media
