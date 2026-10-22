// panels::settings - the live crosshair preview card widget (UTF-8).
//
// The ten part elements mirror the in-game crosshair: five core bars and five
// outline bars, laid out by services::crosshair::Geometry (the same math the
// HUD panel draws with) from the settings drafts - so moving a slider shows
// exactly the geometry the game would draw. The preview is static: the aim
// spread (punch drift) is a runtime fact, not a configuration value.
//
// from: ABCEnchance vgui_controls/CrossHairDisplay.* (the original options
// dialog's preview panel, GBK->UTF-8): same switches (dot/T/outline) and the
// same colour/geometry channels (the RGBA pickers edit the colours now), but
// sharing the production geometry instead of duplicating it.

#include "panels/settings/SettingsPage.h"
#include "panels/settings/sections/CrosshairPreview.h"
#include "services/crosshair/Geometry.h"
#include "services/settings/SettingsModel.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace panels::settings {
namespace {

// Part element ids in services::crosshair::Part order (center, top, bottom,
// left, right) - the settings.rml contracts.
constexpr const char *kCoreIds[services::crosshair::kPartCount] = {
    "cx-core-center", "cx-core-top", "cx-core-bottom", "cx-core-left",
    "cx-core-right"};
constexpr const char *kOutlineIds[services::crosshair::kPartCount] = {
    "cx-outline-center", "cx-outline-top", "cx-outline-bottom",
    "cx-outline-left", "cx-outline-right"};

constexpr const char *kTransparent = "rgba(0, 0, 0, 0)";

int channel(float value) {
  // Draft values are edits of a float setting; inf/NaN and out-of-range
  // drafts must not reach the static_cast (undefined) or the parser.
  if (!std::isfinite(value))
    return 0;
  return std::clamp(static_cast<int>(std::lround(value)), 0, 255);
}

std::string rgba(float red, float green, float blue, float alpha) {
  char Buffer[48];
  std::snprintf(Buffer, sizeof(Buffer), "rgba(%d, %d, %d, %d)", channel(red),
                channel(green), channel(blue), channel(alpha));
  return Buffer;
}

std::string translate(const services::crosshair::Rect &rect) {
  char Buffer[64];
  std::snprintf(Buffer, sizeof(Buffer), "translate(%gpx, %gpx)",
                static_cast<double>(rect.X), static_cast<double>(rect.Y));
  return Buffer;
}

std::string sizeText(float value) {
  char Buffer[32];
  std::snprintf(Buffer, sizeof(Buffer), "%gpx", static_cast<double>(value));
  return Buffer;
}

void applyPart(Rml::Element *element,
               sections::CrosshairPreviewState::Part &applied,
               const char *color, const services::crosshair::Rect &rect) {
  if (!element)
    return;
  const bool same = applied.Valid && applied.Color == color &&
                    applied.Rect.X == rect.X && applied.Rect.Y == rect.Y &&
                    applied.Rect.Width == rect.Width &&
                    applied.Rect.Height == rect.Height;
  if (same)
    return;
  applied.Valid = true;
  applied.Color = color;
  applied.Rect = rect;
  element->SetProperty("background-color", color);
  element->SetProperty("transform", translate(rect));
  element->SetProperty("width", sizeText(rect.Width));
  element->SetProperty("height", sizeText(rect.Height));
}

} // namespace

void SettingsPage::renderCrosshairPreview() {
  Rml::ElementDocument *Doc = document();
  if (!Doc || !Model)
    return;
  Rml::Element *Box = Doc->GetElementById("crosshair-preview");
  if (!Box)
    return;

  // Draft values with the shipped defaults as the fallback (a page is free
  // to omit a row; the preview then shows the documented default).
  const auto Value = [this](const char *Cvar, float fallback) {
    const auto View = Model->find(Cvar);
    if (!View || View->missing)
      return fallback;
    return View->state == services::settings::RowState::kInvalid
               ? View->engineValue
               : View->draftValue;
  };
  services::crosshair::Layout Layout;
  Layout.Thickness = Value("cl_crosshairthickness", 2.0f);
  Layout.Length = Value("cl_crosshairsize", 24.0f);
  Layout.Offset = Value("cl_crosshairgap", 16.0f);
  Layout.OutlineWidth = Value("cl_crosshair_outline_draw", 1.0f) > 0.0f
                            ? Value("cl_crosshair_outline", 1.0f)
                            : 0.0f;
  Layout.Dot = Value("cl_crosshairdot", 0.0f) > 0.0f;
  Layout.TShape = Value("cl_crosshair_t", 0.0f) > 0.0f;

  const std::string CoreColor = rgba(
      Value("cl_crosshaircolor_r", 50.0f), Value("cl_crosshaircolor_g", 250.0f),
      Value("cl_crosshaircolor_b", 50.0f), Value("cl_crosshairalpha", 200.0f));
  const std::string OutlineColor =
      rgba(Value("cl_crosshaircolor_outline_r", 0.0f),
           Value("cl_crosshaircolor_outline_g", 0.0f),
           Value("cl_crosshaircolor_outline_b", 0.0f),
           Value("cl_crosshair_outline_alpha", 255.0f));

  services::crosshair::Rect Core[services::crosshair::kPartCount];
  services::crosshair::Rect Outline[services::crosshair::kPartCount];
  services::crosshair::coreRects(Layout, Core);
  services::crosshair::outlineRects(Layout, Outline);
  for (int I = 0; I < services::crosshair::kPartCount; ++I) {
    const auto Index = static_cast<services::crosshair::Part>(I);
    const bool Shown = services::crosshair::partVisible(Layout, Index);
    applyPart(Doc->GetElementById(kCoreIds[I]), CrosshairPreview.Core[I],
              Shown ? CoreColor.c_str() : kTransparent, Core[I]);
    applyPart(Doc->GetElementById(kOutlineIds[I]), CrosshairPreview.Outline[I],
              (Shown && Layout.OutlineWidth > 0.0f) ? OutlineColor.c_str()
                                                    : kTransparent,
              Outline[I]);
  }
}

} // namespace panels::settings
