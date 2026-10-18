// framework screen metrics implementation - see framework/ScreenMetrics.h.
#include "framework/ScreenMetrics.h"

#include "Render.h"

namespace framework {

int ScreenWidth() {
  return abc::MetaRenderer()
             ? static_cast<int>(abc::MetaRenderer()->GetSwapChainWidth())
             : 1920;
}

int ScreenHeight() {
  return abc::MetaRenderer()
             ? static_cast<int>(abc::MetaRenderer()->GetSwapChainHeight())
             : 480;
}

void ScreenSize(int &width, int &height) {
  width = 1920;
  height = 1080;
  if (abc::MetaRenderer()) {
    width = static_cast<int>(abc::MetaRenderer()->GetSwapChainWidth());
    height = static_cast<int>(abc::MetaRenderer()->GetSwapChainHeight());
  }
}

} // namespace framework
