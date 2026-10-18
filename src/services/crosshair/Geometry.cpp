#include "services/crosshair/Geometry.h"

namespace services::crosshair {
namespace {
int indexOf(Part part) {
  return static_cast<int>(part);
}
} // namespace

bool partVisible(const Layout &layout, Part part) {
  if (part == Part::kCenter)
    return layout.Dot;
  if (part == Part::kTop)
    return !layout.TShape;
  return true;
}

void coreRects(const Layout &layout, Rect out[kPartCount]) {
  const float Width = layout.Thickness;
  const float Length = layout.Length;
  const float HalfWidth = Width / 2.0f;
  const float Inner = layout.Offset + HalfWidth;
  out[indexOf(Part::kCenter)] = {-HalfWidth, -HalfWidth, Width, Width};
  out[indexOf(Part::kTop)] = {-HalfWidth, -Inner - Length, Width, Length};
  out[indexOf(Part::kBottom)] = {-HalfWidth, Inner, Width, Length};
  out[indexOf(Part::kLeft)] = {-Inner - Length, -HalfWidth, Length, Width};
  out[indexOf(Part::kRight)] = {Inner, -HalfWidth, Length, Width};
}

void outlineRects(const Layout &layout, Rect out[kPartCount]) {
  Rect Core[kPartCount];
  coreRects(layout, Core);
  const float Grow = layout.OutlineWidth;
  for (int i = 0; i < kPartCount; ++i) {
    out[i] = {Core[i].X - Grow, Core[i].Y - Grow, Core[i].Width + 2.0f * Grow,
              Core[i].Height + 2.0f * Grow};
  }
}

} // namespace services::crosshair
