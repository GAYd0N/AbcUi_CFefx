// sections::ColorRows implementation - see the header.
#include "panels/settings/sections/ColorRows.h"

#include "services/settings/SettingsModel.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>

namespace panels::settings::sections {
namespace {
// #rrggbbaa: the picker element's canonical value spelling. The short
// #rrggbb form is accepted on input (alpha 255) for hand-written RML.
bool parseHex(const char *text, unsigned char out[4]) {
  if (!text)
    return false;
  const char *digits = text;
  if (*digits == '#')
    ++digits;
  const size_t length = std::strlen(digits);
  if (length != 6 && length != 8)
    return false;
  unsigned int value[4] = {0, 0, 0, 255};
  for (size_t i = 0; i < length / 2; ++i) {
    unsigned int byte = 0;
    for (int half = 0; half < 2; ++half) {
      const char c = digits[i * 2 + half];
      unsigned int nibble = 0;
      if (c >= '0' && c <= '9')
        nibble = static_cast<unsigned int>(c - '0');
      else if (c >= 'a' && c <= 'f')
        nibble = static_cast<unsigned int>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        nibble = static_cast<unsigned int>(c - 'A' + 10);
      else
        return false;
      byte = byte * 16 + nibble;
    }
    value[i] = byte;
  }
  for (int i = 0; i < 4; ++i)
    out[i] = static_cast<unsigned char>(value[i]);
  return true;
}

std::string formatHex(const unsigned char color[4]) {
  char buffer[10];
  std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x%02x", color[0], color[1],
                color[2], color[3]);
  return buffer;
}

int channelValue(float value) {
  if (!std::isfinite(value))
    return 0;
  return std::clamp(static_cast<int>(std::lround(value)), 0, 255);
}

const ColorBinding *bindingFor(const char *elementId) {
  if (!elementId)
    return nullptr;
  for (int i = 0; i < kColorBindingCount; ++i)
    if (std::strcmp(elementId, kColorBindings[i].Element) == 0)
      return &kColorBindings[i];
  return nullptr;
}
} // namespace

const ColorBinding kColorBindings[kColorBindingCount] = {
    {"crosshair-color", "cl_crosshaircolor_r", "cl_crosshaircolor_g",
     "cl_crosshaircolor_b", "cl_crosshairalpha"},
    {"crosshair-outline-color", "cl_crosshaircolor_outline_r",
     "cl_crosshaircolor_outline_g", "cl_crosshaircolor_outline_b",
     "cl_crosshair_outline_alpha"},
};

bool ownsColorCvar(const char *cvar) {
  if (!cvar)
    return false;
  for (int i = 0; i < kColorBindingCount; ++i) {
    const ColorBinding &binding = kColorBindings[i];
    if (std::strcmp(cvar, binding.R) == 0 ||
        std::strcmp(cvar, binding.G) == 0 ||
        std::strcmp(cvar, binding.B) == 0 || std::strcmp(cvar, binding.A) == 0)
      return true;
  }
  return false;
}

void renderColorRows(Rml::ElementDocument *doc,
                     services::settings::SettingsModel *model) {
  if (!doc)
    return;
  for (int i = 0; i < kColorBindingCount; ++i) {
    const ColorBinding &binding = kColorBindings[i];
    Rml::Element *element = doc->GetElementById(binding.Element);
    if (!element)
      continue;
    const char *cvars[4] = {binding.R, binding.G, binding.B, binding.A};
    unsigned char color[4] = {0, 0, 0, 255};
    bool missing = model == nullptr;
    for (int channel = 0; channel < 4; ++channel) {
      const auto view = model ? model->find(cvars[channel]) : std::nullopt;
      if (!view || view->missing) {
        missing = true;
        continue;
      }
      // An invalid draft keeps its text but has no numeric reading; the
      // picker then shows the engine truth instead of a snapped guess.
      const float value = view->state == services::settings::RowState::kInvalid
                              ? view->engineValue
                              : view->draftValue;
      color[channel] = static_cast<unsigned char>(channelValue(value));
    }
    const std::string hex = formatHex(color);
    if (element->GetAttribute<Rml::String>("value", "") != hex)
      element->SetAttribute("value", hex);
    if (missing)
      element->SetAttribute("disabled", "disabled");
    else
      element->RemoveAttribute("disabled");
  }
}

bool handleColorChange(const char *elementId, const char *value,
                       services::settings::SettingsModel *model) {
  const ColorBinding *binding = bindingFor(elementId);
  if (!binding)
    return false;
  unsigned char color[4];
  if (!parseHex(value, color) || !model)
    return true; // picker intent with an unusable payload: consumed, no-op
  const char *cvars[4] = {binding->R, binding->G, binding->B, binding->A};
  for (int channel = 0; channel < 4; ++channel) {
    const auto view = model->find(cvars[channel]);
    if (!view || view->missing)
      return true; // fail-closed: the element is disabled in this state
    char text[8];
    std::snprintf(text, sizeof(text), "%d", static_cast<int>(color[channel]));
    model->setDraftText(cvars[channel], text);
  }
  return true;
}

} // namespace panels::settings::sections
