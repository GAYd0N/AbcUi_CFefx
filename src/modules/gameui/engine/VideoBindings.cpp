// Engine seams extracted from GameUiModule.cpp; no page ownership.
#include <abc/CvarCmd.h>
#include "rmlui/VGUI2ExtensionImport.h"
#include "services/settings/VideoPendingStore.h"
#include <chrono>
#include <cstring>
#include <string_view>
#include "modules/gameui/engine/VideoBindings.h"
namespace gameui::engine {
bool DeferredVideoRestart = false;
bool DeferredVideoConfirmRaise = false;
services::settings::VideoModeModel::Seams videoSeams() {
  services::settings::VideoModeModel::Seams seams;
  seams.actualMode = [](services::settings::VideoMode &out) {
    return vgui2ext::currentVideoMode(out.Width, out.Height, out.Bpp,
                                      out.Windowed);
  };
  seams.modeList = [](services::settings::VideoMode *out, int max) {
    vgui2ext::video::Mode buffer[services::settings::VideoModeModel::kMaxModes];
    if (max > services::settings::VideoModeModel::kMaxModes)
      max = services::settings::VideoModeModel::kMaxModes;
    const int count = vgui2ext::videoModeList(buffer, max);
    for (int i = 0; i < count; ++i) {
      out[i].Width = buffer[i].Width;
      out[i].Height = buffer[i].Height;
      out[i].Bpp = buffer[i].Bpp;
    }
    return count;
  };
  seams.clientCmd = [](const char *command) {
    if (command && std::string_view(command) == "_restart\n") {
      DeferredVideoRestart = true; // never shares the flush with its writes
      return true;
    }
    abc::EngineClientCmd(command);
    return true; // handed to the engine buffer (same contract as bind writes)
  };
  seams.savePending = [](const services::settings::VideoPendingRecord &record) {
    return services::settings::VideoPendingStore::save(record);
  };
  seams.loadPending = [](services::settings::VideoPendingRecord &out) {
    return services::settings::VideoPendingStore::load(out);
  };
  seams.monotonicSeconds = [] {
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
  };
  seams.wallMs = [] {
    using clock = std::chrono::system_clock;
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now().time_since_epoch())
            .count());
  };
  return seams;
}
} // namespace gameui::engine
