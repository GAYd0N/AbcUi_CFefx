// The crosshair colour pickers: the RGBA hex bridge between a picker value
// and the four cvar rows it edits.

#include "../shared/PageShared.h"
#include "SettingsInternal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <cstring>
#include <string>
#include <string_view>

namespace settingspage {

bool parseHex(const char *text, unsigned char out[4]) {
  if (!text)
    return false;
  const char *digits = text;
  if (*digits == '#')
    ++digits;
  const std::size_t length = std::strlen(digits);
  if (length != 6 && length != 8)
    return false;
  unsigned int value[4] = {0, 0, 0, 255};
  for (std::size_t i = 0; i < length / 2; ++i) {
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
  return std::format(
      "#{:02x}{:02x}{:02x}{:02x}", static_cast<unsigned>(color[0]),
      static_cast<unsigned>(color[1]), static_cast<unsigned>(color[2]),
      static_cast<unsigned>(color[3]));
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
    if (std::string_view(elementId) == kColorBindings[i].Element)
      return &kColorBindings[i];
  return nullptr;
}

void renderColorRows() {
  if (!Page || !Api->settings)
    return;
  for (int i = 0; i < kColorBindingCount; ++i) {
    const ColorBinding &binding = kColorBindings[i];
    AbcUiElement *element = node(binding.Element);
    if (!element)
      continue;
    const char *cvars[4] = {binding.R, binding.G, binding.B, binding.A};
    unsigned char color[4] = {0, 0, 0, 255};
    bool missing = false;
    for (int channel = 0; channel < 4; ++channel) {
      AbcUiSettingRow row{};
      if (!rowFound(cvars[channel], row) ||
          (row.flags & ABCUI_SET_ROW_MISSING)) {
        missing = true;
        continue;
      }
      // An invalid draft keeps its text but has no numeric reading; the
      // picker then shows the engine truth instead of a snapped guess.
      const float value = row.state == ABCUI_SET_STATE_INVALID ? row.engineValue
                                                               : row.draftValue;
      color[channel] = static_cast<unsigned char>(channelValue(value));
    }
    const std::string hex = formatHex(color);
    if (attributeOf(element, "value") != hex)
      Api->ui->elementSetAttribute(element, "value", hex.c_str());
    Api->ui->elementSetAttribute(element, "disabled",
                                 missing ? "disabled" : nullptr);
  }
}

void handleColorChange(const char *elementId, const char *value) {
  const ColorBinding *binding = bindingFor(elementId);
  if (!binding)
    return;
  unsigned char color[4];
  if (!parseHex(value, color) || !Api->settings)
    return; // picker intent with an unusable payload: consumed, no-op
  const char *cvars[4] = {binding->R, binding->G, binding->B, binding->A};
  for (int channel = 0; channel < 4; ++channel) {
    AbcUiSettingRow row{};
    if (!rowFound(cvars[channel], row) || (row.flags & ABCUI_SET_ROW_MISSING))
      return; // fail-closed: the element is disabled in this state
    const std::string text =
        std::format("{}", static_cast<int>(color[channel]));
    Api->settings->setDraftText(cvars[channel], text.c_str());
  }
}

} // namespace settingspage
