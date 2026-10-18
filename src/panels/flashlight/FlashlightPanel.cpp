// panels::FlashlightPanel implementation - see FlashlightPanel.h.
// from: ABCEnchance/src/hud/vgui/flashlight.* (GBK->UTF-8).

#include "FlashlightPanel.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>
#include <format>

#include "../../data/HudState.h"
#include "../../data/NetworkMessages.h"
#include "../../framework/ColorUtil.h"
#include "../../framework/GlyphText.h"

namespace panels {

namespace {
// HUD accent amber (255 176 0, base.rcss --abc-hud-accent), the retuned
// replacement for the old scheme FlashLight.IconFgColor "DefaultTransparent"
// (0 255 0 125). The saturation/value of the tint drive the old hue *=
// battery-ratio shift, so a draining battery still walks amber -> red.
constexpr float kIconColor[3] = {255.0f, 176.0f, 0.0f};
constexpr int kIconAlpha = 125;
// Old FlashLightPanel.res: the power wrap's full width the battery ratio
// scales into.
constexpr float kPowerWrapWidth = 23.0f;
} // namespace

FlashlightPanel::FlashlightPanel()
    : HudPanel("flashlight", "abc/abcui/hud/rml/flashlight.rml") {}

void FlashlightPanel::onDocumentReady() {
  setFlashLight(hudstate::messages().FlashOn,
                hudstate::messages().FlashBattery);
  subscribe(netmsg::Flashlight,
            [this](bool on, int battery) { setFlashLight(on, battery); });
  subscribe(netmsg::FlashBat,
            [this](int battery) { setFlashBattery(battery); });
  // Old split: the UpdateClientData handler re-showed on suit alone, the
  // HideWeapon/HideHUD token path applied the flashlight hide bit. Suit
  // state may predate the panel (see HealthPanel note).
  subscribe(hudstate::SuitChanged, [this](bool) { updateVisibility(); });
  subscribe(hudstate::HideTokenChanged, [this](int) { applyHideToken(); });
  updateVisibility();
}

void FlashlightPanel::reset() {
  // Old Reset: hide + battery back to full.
  hide();
  setFlashBattery(100);
}

void FlashlightPanel::updateVisibility() {
  applyHideToken();
}

void FlashlightPanel::applyHideToken() {
  // Old hudhideCallback: ShowPanel(!HIDEFLASHLIGHT) while suited, plus the
  // Viewport-wide hide-all (old SetVisible(false) on the HIDEALL bit).
  const bool visible = hudstate::hasSuit() &&
                       !hudstate::isHudHidden(hudstate::kHideFlashlight) &&
                       !hudstate::isHudHidden(hudstate::kHideAll);
  if (visible)
    show();
  else
    hide();
}

void FlashlightPanel::setFlashLight(bool on, int battery) {
  if (Rml::Element *onImage = element("on-image"))
    onImage->SetProperty("visibility", on ? "visible" : "hidden");
  if (Rml::Element *offImage = element("off-image"))
    offImage->SetProperty("visibility", on ? "hidden" : "visible");
  applyBrightness(on);
  setFlashBattery(battery);
}

void FlashlightPanel::applyBrightness(bool on) {
  // One token drives the whole readout (percentage ink, text fallback, lamp
  // art and gauge); the panel only swaps its value per state.
  if (Document)
    Document->SetProperty("--abc-hud-flashlight-opacity",
                          on ? "0.75" : "0.53");
}

void FlashlightPanel::setFlashBattery(int battery) {
  const float ratio =
      std::clamp(static_cast<float>(battery) / 100.0f, 0.0f, 1.0f);
  if (Rml::Element *wrap = element("power-wrap"))
    wrap->SetProperty(
        "width", Rml::String(std::to_string(ratio * kPowerWrapWidth)) + "dp");

  // Old SetFlashBattery: icon colour hue *= ratio (amber -> red), the
  // scheme alpha rides along (old SetDrawColor Color(rgb, m_cIconColor.a())).
  float hsv[3];
  framework::ColorUtil::rgbToHsv(kIconColor, hsv);
  hsv[0] *= ratio;
  float rgb[3];
  framework::ColorUtil::hsvToRgb(hsv, rgb);
  if (Rml::Element *bar = element("power-bar"))
    bar->SetProperty("image-color",
                     framework::ColorUtil::formatRgba(rgb, kIconAlpha));

  const std::string text = std::format("{}%", battery);
  if (Rml::Element *message = element("message")) {
    // Ink-only additive glyphs (no black outline layer - the readout sits on
    // the sky/lamp art and the ring read as a black backing there), text
    // fallback when a character has no glyph.
    const std::string ink =
        framework::glyphs::inkMarkup(text, framework::glyphs::kAccentTint);
    message->SetInnerRML(ink.empty() ? text : ink);
  }
}

} // namespace panels
