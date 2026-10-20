#pragma once

// sections::ColorRows - the crosshair RGBA colour pickers.
//
// The ABC crosshair used to expose eight sliders (core R/G/B/alpha and
// outline R/G/B/alpha). The page declares the `crosshair-color` widget
// instead: two <colorpicker> elements, one per colour, each editing all four
// channels. The eight cvar rows stay declared in the JSON definition - they
// are the transaction backbone (draft, apply, reset defaults) - but the
// section renderer skips their row scaffolding while the widget owns the
// presentation, exactly like the model-picker owns the `model` row.

namespace Rml {
class ElementDocument;
} // namespace Rml

namespace services::settings {
class SettingsModel;
} // namespace settings

namespace panels::settings::sections {

inline constexpr const char *kColorPickerComponent = "crosshair-color";

// One picker element and the four cvar rows it edits.
struct ColorBinding {
  const char *Element;
  const char *R;
  const char *G;
  const char *B;
  const char *A;
};

inline constexpr int kColorBindingCount = 2;
extern const ColorBinding kColorBindings[kColorBindingCount];

// True for a cvar rendered by a picker rather than by its own row.
bool ownsColorCvar(const char *cvar);

// Projects the draft RGBA into the picker elements (value="#rrggbbaa"), and
// disables a picker whose four rows are not all registered.
void renderColorRows(Rml::ElementDocument *doc,
                     services::settings::SettingsModel *model);

// A picker `change` event: commits the four drafts. Returns true when the
// element id belongs to a picker (the intent is consumed even if the value
// is malformed).
bool handleColorChange(const char *elementId, const char *value,
                       services::settings::SettingsModel *model);

} // namespace panels::settings::sections
