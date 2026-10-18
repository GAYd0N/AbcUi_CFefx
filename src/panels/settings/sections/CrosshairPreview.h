#pragma once

// Per-part applied state of the settings crosshair preview. Defined here so
// the page owns the state (it lives as long as the document) while the
// projection stays in CrosshairPreview.cpp; the guard keeps a per-change
// render from rewriting ten elements' properties when nothing moved.

#include "services/crosshair/Geometry.h"

#include <string>

namespace panels::settings::sections {

struct CrosshairPreviewState {
  struct Part {
    std::string Color;
    services::crosshair::Rect Rect;
    bool Valid = false;
  };
  Part Core[services::crosshair::kPartCount];
  Part Outline[services::crosshair::kPartCount];
};

} // namespace panels::settings::sections
