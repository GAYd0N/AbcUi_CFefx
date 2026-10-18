#pragma once
#include <abc/Math.h>

#include <cmath>
#include <format>
#include <string>

// framework::ColorUtil - RGB <-> HSV conversion used by the danger/hue-shift
// colour rules (old utility RGBtoHSV/HSVtoRGB, HealthPanel.GetDifferColor and
// FlashlightPanel.SetFlashBattery). Inline helpers; guarded against the grey
// (delta == 0) inputs the scheme colours produce.

namespace framework {

namespace ColorUtil {

inline void rgbToHsv(const float rgb[3], float hsv[3]) {
  abc::CMathlib::RGBToHSV(static_cast<int>(rgb[0]), static_cast<int>(rgb[1]),
                          static_cast<int>(rgb[2]), hsv[0], hsv[1], hsv[2]);
}

inline void hsvToRgb(const float hsv[3], float rgb[3]) {
  int r, g, b;
  abc::CMathlib::HSVToRGB(hsv[0], hsv[1], hsv[2], r, g, b);
  rgb[0] = static_cast<float>(r);
  rgb[1] = static_cast<float>(g);
  rgb[2] = static_cast<float>(b);
}

// Old GetDifferColor: pull the hue of `base` toward `danger` by `ratio`
// (ratio 1 = base hue, ratio 0 = danger hue); saturation/value stay base's.
inline void differColor(const float base[3], const float danger[3], float ratio,
                        float out[3]) {
  float hsv[3], dangerHsv[3];
  rgbToHsv(base, hsv);
  rgbToHsv(danger, dangerHsv);
  hsv[0] = dangerHsv[0] + (hsv[0] - dangerHsv[0]) * ratio;
  while (hsv[0] >= 360.0f)
    hsv[0] -= 360.0f;
  while (hsv[0] < 0.0f)
    hsv[0] += 360.0f;
  hsvToRgb(hsv, out);
}

// Formats rgb() for RCSS colour properties (color/image-color).
inline std::string format(const float rgb[3]) {
  return std::format("rgb({}, {}, {})", static_cast<int>(rgb[0]),
                     static_cast<int>(rgb[1]), static_cast<int>(rgb[2]));
}

// rgba() variant - RmlUi takes the alpha as a 0..255 integer (not the CSS
// 0..1 float). Needed for scheme colours that carry real alpha, e.g. the
// flashlight "DefaultTransparent" (0 255 0 125) icon tint.
inline std::string formatRgba(int r, int g, int b, int a) {
  return std::format("rgba({}, {}, {}, {})", r, g, b, a);
}

inline std::string formatRgba(const float rgb[3], int alpha) {
  return formatRgba(static_cast<int>(rgb[0]), static_cast<int>(rgb[1]),
                    static_cast<int>(rgb[2]), alpha);
}

// Player colour swatch (the topcolor / bottomcolor rows). The engine and the
// Renderer treat the 0..255 value as a palette HUE: Renderer's
// PaletteHueReplace (gl_studio.cpp) computes
// `targetHue = value * 1.411764705882353` - exactly value * 360/255 - and
// replaces the player palette's hue band, each entry keeping its own
// saturation and value. A single swatch can only carry the hue, so it is
// shown slightly muted (S/V below pure): the chosen hue stays readable on the
// dark cards without glowing, and it sits closer to the muted tint the
// palette replacement actually produces.
constexpr float kPlayerHueDegreesPerStep = 360.0f / 255.0f;
constexpr float kPlayerHueSaturation = 0.8f;
constexpr float kPlayerHueValue = 0.9f;

// value (clamped to 0..255) -> the swatch colour as rounded RGB bytes in
// floats, ready for format(). 0 and 255 are both the full red end of the
// wheel, matching the engine's own wrap.
inline void playerHueColor(float value, float rgb[3]) {
  if (!(value > 0.0f))
    value = 0.0f;
  if (value > 255.0f)
    value = 255.0f;
  const float hp = value * kPlayerHueDegreesPerStep / 60.0f;
  const float chroma = kPlayerHueValue * kPlayerHueSaturation;
  const float second = chroma * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
  float r = 0.0f, g = 0.0f, b = 0.0f;
  if (hp < 1.0f) {
    r = chroma;
    g = second;
  } else if (hp < 2.0f) {
    r = second;
    g = chroma;
  } else if (hp < 3.0f) {
    g = chroma;
    b = second;
  } else if (hp < 4.0f) {
    g = second;
    b = chroma;
  } else if (hp < 5.0f) {
    r = second;
    b = chroma;
  } else {
    r = chroma;
    b = second;
  }
  const float base = kPlayerHueValue - chroma;
  rgb[0] = std::floor((r + base) * 255.0f + 0.5f);
  rgb[1] = std::floor((g + base) * 255.0f + 0.5f);
  rgb[2] = std::floor((b + base) * 255.0f + 0.5f);
}

} // namespace ColorUtil

} // namespace framework
