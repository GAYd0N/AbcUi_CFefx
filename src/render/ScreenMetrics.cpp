// framework screen metrics implementation - see framework/ScreenMetrics.h.
#include "framework/ScreenMetrics.h"

#include "BackendHost.h"
#include "IRenderBackend.h"

namespace framework {

int ScreenWidth() {
  const auto *backend = abc::render::backend();
  return backend ? backend->surfaceWidth() : 1920;
}

int ScreenHeight() {
  const auto *backend = abc::render::backend();
  return backend ? backend->surfaceHeight() : 480;
}

void ScreenSize(int &width, int &height) {
  width = 1920;
  height = 1080;
  if (const auto *backend = abc::render::backend()) {
    width = backend->surfaceWidth();
    height = backend->surfaceHeight();
  }
}

} // namespace framework
