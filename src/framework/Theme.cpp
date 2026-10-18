// framework::Theme implementation - see Theme.h.
#include "Theme.h"

#include <RmlUi/Core/Context.h>

#include "ScreenMetrics.h"

#include <algorithm>

namespace framework {

namespace Theme {

namespace {
float CurrentScale = 1.0f;
}

void applyScale(Rml::Context *context) {
  const float height = static_cast<float>(framework::ScreenHeight());
  CurrentScale = std::clamp(height / 480.0f, 0.25f, 8.0f);
  if (context)
    context->SetDensityIndependentPixelRatio(CurrentScale);
}

float scale() {
  return CurrentScale;
}

} // namespace Theme

} // namespace framework
