// Pure page formatting and DOM action lookup.
#include "panels/settings/SettingsUi.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include "services/settings/SettingsModel.h"
#include "framework/Localization.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
namespace panels::settings::detail {
Rml::Element *actionNode(Rml::Element *node) {
  for (Rml::Element *it = node; it; it = it->GetParentNode()) {
    if (it->HasAttribute("data-action"))
      return it;
  }
  return nullptr;
}

std::string translated(const char *token) {
  // "#token" -> lang table; an unknown token degrades to the raw token
  // (the documented Localization fallback), still escaped by the caller.
  std::string out;
  if (token && *token == '#') {
    if (!framework::Localization::translate(out, token + 1))
      out = token;
  } else if (token) {
    out = token;
  }
  return out;
}

std::string numberText(float value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
  return buffer;
}

std::string logoColorName(const std::string &value) {
  std::string name = value;
  if (!name.empty() && name[0] == '#')
    name.erase(0, 1);
  constexpr const char *kValvePrefix = "Valve_";
  if (name.rfind(kValvePrefix, 0) == 0)
    name.erase(0, std::strlen(kValvePrefix));
  return name;
}

bool isOn(float value) {
  return value > 0.5f;
}

bool parseNumber(const char *text, float &out) {
  if (!text || !*text)
    return false;
  char *end = nullptr;
  const double parsed = std::strtod(text, &end);
  if (end != text + std::strlen(text) || !std::isfinite(parsed))
    return false;
  out = static_cast<float>(parsed);
  return true;
}

bool isToggleKind(services::settings::SettingKind kind) {
  return kind == services::settings::SettingKind::kBool ||
         kind == services::settings::SettingKind::kNegate ||
         kind == services::settings::SettingKind::kCommandToggle;
}

bool isSelectKind(services::settings::SettingKind kind) {
  return kind == services::settings::SettingKind::kEnum ||
         kind == services::settings::SettingKind::kStringEnum;
}

bool toggleDraftOn(const services::settings::RowView &view) {
  if (view.row->Kind == services::settings::SettingKind::kNegate)
    return view.draftValue < 0.0f;
  return isOn(view.draftValue);
}

const char *safeFocusForLayer(const char *id) {
  if (std::string(id) == kLayerReset)
    return "reset-no";
  if (std::string(id) == kLayerBindConflict)
    return "bind-conflict-no";
  if (std::string(id) == kLayerBindReset)
    return "bind-reset-no";
  if (std::string(id) == kLayerSprayFile)
    return "spray-file-cancel";
  return "leave-stay";
}

// One row per section component that has a widget block in settings.rml.
// The same names the definition parser accepts; a name without a block here
// would be a schema/asset mismatch the page test catches.
const WidgetBlock kWidgetBlocks[] = {
    {"model-preview", "widget-model-preview", true, false},
    {"model-picker", "widget-model-picker", false, true},
    {"spray-preview", "widget-spray-preview", true, false},
    {"crosshair-preview", "widget-crosshair-preview", true, false},
    {"crosshair-color", "widget-crosshair-color", false, false},
    {"spray-maker", "widget-spray-maker", true, false},
    {"bindings", "widget-bindings", false, false},
    {"video", "widget-video", false, false},
    {"voice", "widget-voice", false, false},
};
const int kWidgetBlockCount =
    static_cast<int>(sizeof(kWidgetBlocks) / sizeof(kWidgetBlocks[0]));
} // namespace panels::settings::detail
