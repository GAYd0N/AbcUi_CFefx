#pragma once

// <colorpicker> - a self-drawn RGBA colour picker (settings pages).
//
// The widget is one element: a hue strip, a saturation/value square and an
// alpha strip, all painted as untextured vertex-colour geometry through
// RmlUi's render interface (no gradient decorators - the renderer behind the
// interface has no shader path). A press/drag on any strip updates the
// colour; every change sets the canonical `value` attribute ("#rrggbbaa") and
// dispatches a `change` event carrying the same string, so the page commits
// the four drafts exactly like an ordinary row control.
//
// Attributes:
//   value    "#rrggbbaa" (or "#rrggbb"); parsed case-insensitively.
//   disabled when present, the picker paints a dim overlay and ignores input.
//
// The element owns its compiled geometry and releases it on destruction.
// Painting happens in OnRender, inside the element's clip region, so the
// page's scroll container clips it like any other box.

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Types.h>

namespace Rml {
class ElementDocument;
class Event;
} // namespace Rml

namespace rmlui::colorpicker {

class ElementColorPicker : public Rml::Element {
public:
  explicit ElementColorPicker(const Rml::String &Tag);
  ~ElementColorPicker() override;

  // Dispatches the queued `change` events for every live picker. Called from
  // the frame pump, never from the drag/event stack: the page's handler may
  // destroy the picker (or another one), which is only safe once the element
  // event dispatch has returned to the frame loop.
  static void pumpChanges();

protected:
  void OnAttributeChange(const Rml::ElementAttributes &Changed) override;
  void OnRender() override;

private:
  enum class Region { None, Hue, SaturationValue, Alpha };

  struct Layout {
    float Padding = 0.0f;
    float HueX = 0.0f, HueY = 0.0f, HueW = 0.0f, HueH = 0.0f;
    float SvX = 0.0f, SvY = 0.0f, SvW = 0.0f, SvH = 0.0f;
    float AlphaX = 0.0f, AlphaY = 0.0f, AlphaW = 0.0f, AlphaH = 0.0f;
  };

  // Pointer drag: the press picks the strip, the document keeps the drag
  // alive while the pointer leaves the element.
  class DragListener final : public Rml::EventListener {
  public:
    explicit DragListener(ElementColorPicker *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    ElementColorPicker *Owner;
  };

  Layout layout(Rml::Vector2f size);
  void beginDrag(const Layout &area, float x, float y);
  void updateDrag(const Layout &area, float x, float y, Region region);
  void endDrag();
  void applyColor();
  void setFromRgba(int red, int green, int blue, int alpha);
  void parseValue(const Rml::String &text);
  void rebuildGeometry(Rml::Vector2f size);
  void releaseGeometry();
  void syncDocumentListeners(bool listen);

  DragListener Listener;
  Rml::ElementDocument *ListenedDocument = nullptr;
  Rml::CompiledGeometryHandle Geometry = 0;
  Rml::Vector2f GeometrySize{-1.0f, -1.0f};
  bool GeometryDirty = true;
  bool Disabled = false;
  bool Dragging = false;
  Region DragRegion = Region::None;

  // Editing state: HSV keeps the hue/saturation stable across the RGB
  // round-trips (a grey pixel must not forget which hue produced it).
  float Hue = 0.0f;
  float Saturation = 1.0f;
  float Value = 1.0f;
  int Alpha = 255;

  // Latest value whose `change` event has not been dispatched yet; queued by
  // applyColor and drained by pumpChanges.
  bool ChangePending = false;
  Rml::String PendingValue;
};

void registerElement();

} // namespace rmlui::colorpicker
