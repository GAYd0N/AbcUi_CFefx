// abcui.flashlight - flashlight icon/battery readout. Moved from
// src/panels/flashlight (old ABCEnchance FlashLightPanel.res layout):
// visibility rides the suit gate plus the flashlight/all hide tokens, the
// lamp art swaps on the Flashlight message, the gauge and the percentage
// follow FlashBat, and the icon hue walks amber -> red as the battery
// drains (module-local color math where the built-in used ColorUtil).
#include <abcui/events.h>
#include <abcui/module_api.h>
#include "../../common/Glyphs.h"

#include <algorithm>
#include <cstdio>
#include <format>

namespace {

const AbcUiModuleApi *Api = nullptr;
moduleui::Glyphs GlyphInk;
AbcUiPanelDesc Panel = {};

// HUD accent amber (255 176 0, base.rcss --abc-hud-accent); saturation and
// value drive the old hue *= battery-ratio shift.
constexpr float kIconColor[3] = {255.0f, 176.0f, 0.0f};
constexpr int kIconAlpha = 125;
// Old FlashLightPanel.res: the power wrap's full width the battery ratio
// scales into.
constexpr float kPowerWrapWidth = 23.0f;

constexpr const char *kPanelName = "flashlight";
constexpr const char *kDocumentPath =
    "mod://abcui.flashlight/hud/rml/flashlight.rml";

AbcUiDocument *panelDocument() {
  return Api->ui->panelDocument(kPanelName);
}

void elementSetProperty(const char *id, const char *name, const char *value) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (AbcUiElement *element = Api->ui->querySelector(doc, id))
    Api->ui->elementSetProperty(element, name, value);
}

void applyBrightness(int on) {
  if (AbcUiDocument *doc = panelDocument()) {
    Api->ui->documentSetProperty(doc, "--abc-hud-flashlight-opacity",
                                 on ? "0.75" : "0.53");
  }
}

void rgbToHsv(const float rgb[3], float hsv[3]) {
  const float max = std::max(std::max(rgb[0], rgb[1]), rgb[2]);
  const float min = std::min(std::min(rgb[0], rgb[1]), rgb[2]);
  const float delta = max - min;
  hsv[2] = max;
  hsv[1] = max > 0.0f ? delta / max : 0.0f;
  if (delta <= 0.0f) {
    hsv[0] = 0.0f;
    return;
  }
  if (max == rgb[0])
    hsv[0] = (rgb[1] - rgb[2]) / delta + (rgb[1] < rgb[2] ? 6.0f : 0.0f);
  else if (max == rgb[1])
    hsv[0] = (rgb[2] - rgb[0]) / delta + 2.0f;
  else
    hsv[0] = (rgb[0] - rgb[1]) / delta + 4.0f;
  hsv[0] /= 6.0f;
}

void hsvToRgb(const float hsv[3], float rgb[3]) {
  const float hue = hsv[0] * 6.0f;
  const int sector = static_cast<int>(hue);
  const float fraction = hue - sector;
  const float value = hsv[2];
  const float saturation = hsv[1];
  const float low = value * (1.0f - saturation);
  const float mid = value * (1.0f - saturation * fraction);
  const float high = value * (1.0f - saturation * (1.0f - fraction));
  switch (sector % 6) {
  case 0:
    rgb[0] = value;
    rgb[1] = high;
    rgb[2] = low;
    break;
  case 1:
    rgb[0] = mid;
    rgb[1] = value;
    rgb[2] = low;
    break;
  case 2:
    rgb[0] = low;
    rgb[1] = value;
    rgb[2] = high;
    break;
  case 3:
    rgb[0] = low;
    rgb[1] = mid;
    rgb[2] = value;
    break;
  case 4:
    rgb[0] = high;
    rgb[1] = low;
    rgb[2] = value;
    break;
  default:
    rgb[0] = value;
    rgb[1] = low;
    rgb[2] = mid;
    break;
  }
}

void setFlashBattery(int battery) {
  const float ratio =
      std::clamp(static_cast<float>(battery) / 100.0f, 0.0f, 1.0f);
  std::string value = std::format("{:g}dp", ratio * kPowerWrapWidth);
  elementSetProperty("power-wrap", "width", value.c_str());

  // Old SetFlashBattery: icon colour hue *= ratio (amber -> red), the
  // scheme alpha rides along.
  float hsv[3];
  float rgb[3];
  const float base[3] = {kIconColor[0] / 255.0f, kIconColor[1] / 255.0f,
                         kIconColor[2] / 255.0f};
  rgbToHsv(base, hsv);
  hsv[0] *= ratio;
  hsvToRgb(hsv, rgb);
  value = std::format("rgba({}, {}, {}, {})",
                      static_cast<int>(rgb[0] * 255.0f + 0.5f),
                      static_cast<int>(rgb[1] * 255.0f + 0.5f),
                      static_cast<int>(rgb[2] * 255.0f + 0.5f), kIconAlpha);
  elementSetProperty("power-bar", "image-color", value.c_str());

  value = std::format("{}%", battery);
  const std::string ink = GlyphInk.ink(Api, value);
  const char *markup = ink.empty() ? value.c_str() : ink.c_str();
  if (AbcUiDocument *doc = panelDocument()) {
    if (AbcUiElement *element = Api->ui->querySelector(doc, "message"))
      Api->ui->elementSetInnerRML(element, markup);
  }
}

void setFlashLight(int on, int battery) {
  elementSetProperty("on-image", "visibility", on ? "visible" : "hidden");
  elementSetProperty("off-image", "visibility", on ? "hidden" : "visible");
  applyBrightness(on);
  setFlashBattery(battery);
}

void updateVisibility() {
  // Old hudhideCallback: ShowPanel(!HIDEFLASHLIGHT) while suited, plus the
  // viewport-wide hide-all bit.
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  const int visible = state.suit &&
                      !(state.hideBits & ABCUI_HIDEBIT_FLASHLIGHT) &&
                      !(state.hideBits & ABCUI_HIDEBIT_ALL);
  if (AbcUiDocument *doc = panelDocument()) {
    if (visible)
      Api->ui->documentShow(doc);
    else
      Api->ui->documentHide(doc);
  }
}

// ---- event sinks (elements re-queried per event; no cached handles) ----

void onFlashlight(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiFlashlightEvent *>(payload);
  setFlashLight(event->on, event->battery);
}

void onFlashBat(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiFlashBatEvent *>(payload);
  setFlashBattery(event->battery);
}

void onSuitChanged(void *, const void *) {
  updateVisibility();
}

void onHideToken(void *, const void *) {
  updateVisibility();
}

// ---- panel callbacks ----

void panelDocumentReady(void *) {
  // Suit state may predate the panel; seed from the current snapshot.
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  setFlashLight(state.flashOn, state.flashBattery);
  updateVisibility();
}

void panelReset(void *) {
  // Old Reset: hide + battery back to full.
  if (AbcUiDocument *doc = panelDocument())
    Api->ui->documentHide(doc);
  setFlashBattery(100);
}

// ---- lifecycle ----

int onNativeHudDraw(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiNativeHudEvent *>(payload);
  return panelDocument() && (event->element == ABCUI_NATIVE_HUD_FLASHLIGHT);
}

void onModuleLoad() {
  Api->events->subscribeVeto(ABCUI_CHANNEL_HUD_NATIVE_DRAW, 0, &onNativeHudDraw,
                             nullptr);
  Panel.structSize = sizeof(Panel);
  Panel.name = kPanelName;
  Panel.documentPath = kDocumentPath;
  Panel.zIndex = 0;
  Panel.userdata = nullptr;
  Panel.onDocumentReady = &panelDocumentReady;
  Panel.onData = nullptr;
  Panel.onRedraw = nullptr;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);

  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_FLASHLIGHT, 0, &onFlashlight,
                         nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_FLASHBAT, 0, &onFlashBat,
                         nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_SUIT, 0, &onSuitChanged, nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_HIDE_TOKEN, 0, &onHideToken,
                         nullptr);
}

void onModuleShutdown() {
  GlyphInk.reset();
}

void onModuleUnload() {
  // Subscriptions are process-lifetime by design; the bus outlives every
  // module (the framework unloads in reverse order after Rml is down).
  Api = nullptr;
}
} // namespace

ABCUI_MODULE_EXPORT int ABCUI_CALL
AbcUiModule_Main(uint32_t abiVersion, const AbcUiModuleApi *api,
                 AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api ||
      !AbcUiModule_InitDescriptor(out))
    return 0;
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->panels ||
      !api->textures || !api->events || !api->data || !api->ui)
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.flashlight";
  out->version = "2.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = nullptr;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
