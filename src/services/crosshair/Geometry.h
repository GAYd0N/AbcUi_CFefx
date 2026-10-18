#pragma once

// services::crosshair::Geometry - the single implementation of the dynamic
// crosshair layout, shared by the in-game panel (panels::CrosshairPanel) and
// the settings preview. Pure float math on the cvar values: no Rml, no engine,
// no screen state. The caller owns where the centre is (the HUD centre, the
// third-person projection, the preview box) and how the punch drift is
// computed - this file only answers "where do the five parts and their five
// outline siblings sit around that centre".
//
// Values are the old ABCEnchance pixel units (cl_crosshair* are screen-pixel
// cvars, not dp), so both consumers draw the exact geometry the game draws.

namespace services::crosshair {

enum class Part {
  kCenter,
  kTop,
  kBottom,
  kLeft,
  kRight,
};

inline constexpr int kPartCount = 5;

// One layout request: the cvar values after the caller applied its own gates
// (outline on/off, T shape, drift added to the gap).
struct Layout {
  float Thickness = 2.0f;    // cl_crosshairthickness
  float Length = 24.0f;      // cl_crosshairsize
  float Offset = 16.0f;      // cl_crosshairgap + drift
  float OutlineWidth = 1.0f; // cl_crosshair_outline (0 draws no outline)
  bool Dot = false;          // cl_crosshairdot
  bool TShape = false;       // cl_crosshair_t (hides the top bar)
};

// A rectangle relative to the crosshair centre (X/Y may be negative).
struct Rect {
  float X = 0.0f;
  float Y = 0.0f;
  float Width = 0.0f;
  float Height = 0.0f;
};

// False for the parts the switches hide (top bar in T shape, centre dot off):
// the caller paints them transparent.
bool partVisible(const Layout &layout, Part part);

// Core bars around the centre, in Part order.
void coreRects(const Layout &layout, Rect out[kPartCount]);

// Outline bars: each core rect grown by OutlineWidth on every side.
void outlineRects(const Layout &layout, Rect out[kPartCount]);

} // namespace services::crosshair
