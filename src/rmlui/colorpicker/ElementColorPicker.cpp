// <colorpicker> implementation - see the header.
#include "ElementColorPicker.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/RenderInterface.h>

#include <abc/Math.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <unordered_set>
#include <vector>

namespace rmlui::colorpicker {
namespace {
// Pickers whose `change` event is queued and pickers that are still alive
// (the destructor removes itself). pumpChanges uses these to skip elements a
// page handler destroyed while the queue was being drained.
std::vector<ElementColorPicker *> &PendingChanges() {
  static std::vector<ElementColorPicker *> pending;
  return pending;
}
std::unordered_set<const ElementColorPicker *> &LiveElements() {
  static std::unordered_set<const ElementColorPicker *> live;
  return live;
}
// Strip heights and gutters in dp; the density ratio scales them to pixels.
constexpr float kHueHeight = 10.0f;
constexpr float kAlphaHeight = 10.0f;
constexpr float kGap = 4.0f;
constexpr float kMinimumSvHeight = 8.0f;
constexpr float kHueSegments = 24.0f;
constexpr float kSvColumns = 24.0f;
constexpr int kSvRows = 8;
constexpr float kAlphaSegments = 24.0f;
constexpr float kMarkerDp = 3.0f;

Rml::ColourbPremultiplied premultiplied(int red, int green, int blue,
                                        int alpha) {
  const auto channel = [alpha](int value) {
    return static_cast<Rml::byte>((std::clamp(value, 0, 255) * alpha) / 255);
  };
  return Rml::ColourbPremultiplied(channel(red), channel(green), channel(blue),
                                   static_cast<Rml::byte>(alpha));
}

struct MeshBuilder {
  std::vector<Rml::Vertex> Vertices;
  std::vector<int> Indices;

  void addVertex(float x, float y, Rml::ColourbPremultiplied colour) {
    Rml::Vertex vertex;
    vertex.position = Rml::Vector2f(x, y);
    vertex.colour = colour;
    vertex.tex_coord = Rml::Vector2f(0.0f, 0.0f);
    Vertices.push_back(vertex);
  }

  void addQuad(float x, float y, float width, float height,
               Rml::ColourbPremultiplied colour) {
    if (width <= 0.0f || height <= 0.0f)
      return;
    const int base = static_cast<int>(Vertices.size());
    addVertex(x, y, colour);
    addVertex(x + width, y, colour);
    addVertex(x + width, y + height, colour);
    addVertex(x, y + height, colour);
    Indices.insert(Indices.end(),
                   {base, base + 1, base + 2, base, base + 2, base + 3});
  }

  // Vertex-colour gradient over a rectangle: (s, t) are the unit
  // coordinates of each corner, so a hue strip and an HSV square share one
  // path.
  void addGradient(
      float x, float y, float width, float height, int columns, int rows,
      const std::function<Rml::ColourbPremultiplied(float, float)> &colour) {
    if (width <= 0.0f || height <= 0.0f || columns <= 0 || rows <= 0)
      return;
    const int base = static_cast<int>(Vertices.size());
    for (int row = 0; row <= rows; ++row) {
      const float t = static_cast<float>(row) / static_cast<float>(rows);
      for (int column = 0; column <= columns; ++column) {
        const float s =
            static_cast<float>(column) / static_cast<float>(columns);
        addVertex(x + width * s, y + height * t, colour(s, t));
      }
    }
    const int stride = columns + 1;
    for (int row = 0; row < rows; ++row) {
      for (int column = 0; column < columns; ++column) {
        const int index = base + row * stride + column;
        Indices.insert(Indices.end(),
                       {index, index + 1, index + stride + 1, index,
                        index + stride + 1, index + stride});
      }
    }
  }
};

bool parseHex(const char *text, int rgba[4]) {
  if (!text)
    return false;
  const char *digits = text;
  if (*digits == '#')
    ++digits;
  const size_t length = std::strlen(digits);
  if (length != 6 && length != 8)
    return false;
  rgba[0] = rgba[1] = rgba[2] = 0;
  rgba[3] = 255;
  for (size_t i = 0; i < length / 2; ++i) {
    int byte = 0;
    for (int half = 0; half < 2; ++half) {
      const char c = digits[i * 2 + half];
      int nibble = -1;
      if (c >= '0' && c <= '9')
        nibble = c - '0';
      else if (c >= 'a' && c <= 'f')
        nibble = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        nibble = c - 'A' + 10;
      if (nibble < 0)
        return false;
      byte = byte * 16 + nibble;
    }
    rgba[i] = byte;
  }
  return true;
}

Rml::ElementInstancerGeneric<ElementColorPicker> Instancer;
} // namespace

ElementColorPicker::ElementColorPicker(const Rml::String &Tag)
    : Element(Tag), Listener(this) {
  SetProperty("cursor", "crosshair");
  // Only the press is bound to the element; the document takes the move and
  // release while the drag is alive.
  AddEventListener("mousedown", &Listener, false);
  LiveElements().insert(this);
}

ElementColorPicker::~ElementColorPicker() {
  LiveElements().erase(this);
  std::erase(PendingChanges(), this);
  syncDocumentListeners(false);
  releaseGeometry();
}

void ElementColorPicker::OnAttributeChange(
    const Rml::ElementAttributes &Changed) {
  Element::OnAttributeChange(Changed);
  if (Changed.find("value") != Changed.end())
    parseValue(GetAttribute<Rml::String>("value", ""));
  if (Changed.find("disabled") != Changed.end()) {
    Disabled = HasAttribute("disabled");
    GeometryDirty = true;
  }
}

void ElementColorPicker::parseValue(const Rml::String &text) {
  int rgba[4];
  if (!parseHex(text.c_str(), rgba))
    return;
  setFromRgba(rgba[0], rgba[1], rgba[2], rgba[3]);
}

void ElementColorPicker::setFromRgba(int red, int green, int blue, int alpha) {
  float hue = 0.0f, saturation = 0.0f, value = 0.0f;
  abc::math::RGBToHSV(red, green, blue, hue, saturation, value);
  // A grey has no hue of its own and black has no saturation: keep the
  // edited channel through those corners so a drag along the SV edge does
  // not collapse the other axis.
  if (saturation > 0.0001f)
    Hue = hue;
  if (value > 0.0001f)
    Saturation = saturation;
  Value = value;
  Alpha = std::clamp(alpha, 0, 255);
  GeometryDirty = true;
}

void ElementColorPicker::applyColor() {
  int red = 0, green = 0, blue = 0;
  abc::math::HSVToRGB(Hue, Saturation, Value, red, green, blue);
  char text[10];
  std::snprintf(text, sizeof(text), "#%02x%02x%02x%02x", red, green, blue,
                Alpha);
  GeometryDirty = true;
  if (GetAttribute<Rml::String>("value", "") != text)
    SetAttribute("value", Rml::String(text));
  // Do not dispatch here: this runs inside the element's own drag/render
  // call stack, and the page's change handler may destroy the picker right
  // away. Queue the latest value and let pumpChanges fire it from the frame
  // loop instead.
  PendingValue = text;
  if (!ChangePending) {
    ChangePending = true;
    PendingChanges().push_back(this);
  }
}

void ElementColorPicker::pumpChanges() {
  auto &pending = PendingChanges();
  if (pending.empty())
    return;
  std::vector<ElementColorPicker *> queued;
  queued.swap(pending);
  for (ElementColorPicker *element : queued) {
    // A handler earlier in this drain may have destroyed this picker (its
    // destructor removed it from the live set and the queue).
    if (!LiveElements().contains(element))
      continue;
    element->ChangePending = false;
    const Rml::String value = element->PendingValue;
    Rml::Dictionary parameters;
    parameters["value"] = Rml::Variant(value);
    element->DispatchEvent("change", parameters);
    // The handler may have destroyed `element`; do not touch it again. Its
    // destructor already removed it from the live set.
  }
}

ElementColorPicker::Layout ElementColorPicker::layout(Rml::Vector2f size) {
  const float ratio =
      Rml::ElementUtilities::GetDensityIndependentPixelRatio(this);
  Layout area;
  area.HueX = 0.0f;
  area.HueY = 0.0f;
  area.HueW = std::max(0.0f, size.x);
  area.HueH = std::max(1.0f, kHueHeight * ratio);
  const float gap = std::max(1.0f, kGap * ratio);
  area.AlphaX = 0.0f;
  area.AlphaH = std::max(1.0f, kAlphaHeight * ratio);
  area.AlphaW = area.HueW;
  area.AlphaY = std::max(0.0f, size.y - area.AlphaH);
  area.SvX = 0.0f;
  area.SvY = area.HueY + area.HueH + gap;
  area.SvW = area.HueW;
  area.SvH = area.AlphaY - gap - area.SvY;
  area.SvH = std::max(kMinimumSvHeight, area.SvH);
  area.Padding = gap;
  return area;
}

void ElementColorPicker::beginDrag(const Layout &area, float x, float y) {
  if (Disabled)
    return;
  if (y >= area.HueY && y < area.HueY + area.HueH)
    DragRegion = Region::Hue;
  else if (y >= area.SvY && y < area.SvY + area.SvH)
    DragRegion = Region::SaturationValue;
  else if (y >= area.AlphaY && y < area.AlphaY + area.AlphaH)
    DragRegion = Region::Alpha;
  else
    return;
  Dragging = true;
  // The region stays latched for the whole drag: moving the pointer across
  // strips never switches what is being edited.
  updateDrag(area, x, y, DragRegion);
  syncDocumentListeners(true);
}

void ElementColorPicker::updateDrag(const Layout &area, float x, float y,
                                    Region region) {
  const auto unit = [](float position, float length) {
    if (length <= 0.0f)
      return 0.0f;
    return std::clamp(position / length, 0.0f, 1.0f);
  };
  switch (region) {
  case Region::Hue:
    Hue = unit(x - area.HueX, area.HueW) * 359.999f;
    break;
  case Region::SaturationValue:
    Saturation = unit(x - area.SvX, area.SvW);
    Value = 1.0f - unit(y - area.SvY, area.SvH);
    break;
  case Region::Alpha:
    Alpha = static_cast<int>(
        std::lround(unit(x - area.AlphaX, area.AlphaW) * 255.0f));
    break;
  case Region::None:
    return;
  }
  applyColor();
}

void ElementColorPicker::endDrag() {
  if (!Dragging)
    return;
  Dragging = false;
  DragRegion = Region::None;
  syncDocumentListeners(false);
}

void ElementColorPicker::syncDocumentListeners(bool listen) {
  Rml::ElementDocument *document = GetOwnerDocument();
  if (listen) {
    if (!document || ListenedDocument == document)
      return;
    ListenedDocument = document;
    document->AddEventListener("mousemove", &Listener, false);
    document->AddEventListener("mouseup", &Listener, false);
    return;
  }
  if (!ListenedDocument)
    return;
  ListenedDocument->RemoveEventListener("mousemove", &Listener, false);
  ListenedDocument->RemoveEventListener("mouseup", &Listener, false);
  ListenedDocument = nullptr;
}

void ElementColorPicker::DragListener::ProcessEvent(Rml::Event &event) {
  const Rml::EventId id = event.GetId();
  if (id == Rml::EventId::Mousedown) {
    if (event.GetParameter<int>("button", 0) != 0)
      return;
    if (!Owner->GetOwnerDocument())
      return;
    const auto content = Owner->GetBox().GetSize(Rml::BoxArea::Content);
    const Rml::Vector2f origin =
        Owner->GetAbsoluteOffset(Rml::BoxArea::Content);
    Owner->beginDrag(Owner->layout(content),
                     event.GetParameter<float>("mouse_x", 0.0f) - origin.x,
                     event.GetParameter<float>("mouse_y", 0.0f) - origin.y);
    return;
  }
  if (!Owner->Dragging)
    return;
  if (id == Rml::EventId::Mouseup) {
    Owner->endDrag();
    return;
  }
  if (id == Rml::EventId::Mousemove) {
    // The drag is re-created against the current box so a resize during the
    // gesture keeps the coordinates honest.
    const auto content = Owner->GetBox().GetSize(Rml::BoxArea::Content);
    const Rml::Vector2f origin =
        Owner->GetAbsoluteOffset(Rml::BoxArea::Content);
    Owner->updateDrag(Owner->layout(content),
                      event.GetParameter<float>("mouse_x", 0.0f) - origin.x,
                      event.GetParameter<float>("mouse_y", 0.0f) - origin.y,
                      Owner->DragRegion);
  }
}

void ElementColorPicker::releaseGeometry() {
  if (!Geometry)
    return;
  if (Rml::RenderInterface *renderer = Rml::GetRenderInterface())
    renderer->ReleaseGeometry(Geometry);
  Geometry = 0;
}

void ElementColorPicker::rebuildGeometry(Rml::Vector2f size) {
  releaseGeometry();
  GeometrySize = size;
  GeometryDirty = false;

  const float ratio =
      Rml::ElementUtilities::GetDensityIndependentPixelRatio(this);
  const Layout area = layout(size);
  MeshBuilder mesh;

  mesh.addGradient(area.HueX, area.HueY, area.HueW, area.HueH,
                   static_cast<int>(kHueSegments), 1, [](float s, float) {
                     int red = 0, green = 0, blue = 0;
                     abc::math::HSVToRGB(s * 359.999f, 1.0f, 1.0f, red, green,
                                         blue);
                     return premultiplied(red, green, blue, 255);
                   });

  // Saturation grows left to right, value top to bottom.
  mesh.addGradient(area.SvX, area.SvY, area.SvW, area.SvH,
                   static_cast<int>(kSvColumns), kSvRows,
                   [this](float s, float t) {
                     int red = 0, green = 0, blue = 0;
                     abc::math::HSVToRGB(Hue, s, 1.0f - t, red, green, blue);
                     return premultiplied(red, green, blue, 255);
                   });

  // Alpha strip: a checkerboard proves the transparency, the gradient
  // over it carries the colour.
  const float cell = std::max(3.0f, 4.0f * ratio);
  const int columns =
      std::max(1, static_cast<int>(std::ceil(area.AlphaW / cell)));
  const int rows = std::max(1, static_cast<int>(std::ceil(area.AlphaH / cell)));
  for (int row = 0; row < rows; ++row) {
    for (int column = 0; column < columns; ++column) {
      const float x = area.AlphaX + column * cell;
      const float y = area.AlphaY + row * cell;
      const float width = std::min(cell, area.AlphaX + area.AlphaW - x);
      const float height = std::min(cell, area.AlphaY + area.AlphaH - y);
      const bool light = ((row + column) % 2) == 0;
      mesh.addQuad(x, y, width, height,
                   premultiplied(light ? 62 : 34, light ? 70 : 42,
                                 light ? 78 : 48, 255));
    }
  }
  int red = 0, green = 0, blue = 0;
  abc::math::HSVToRGB(Hue, Saturation, Value, red, green, blue);
  mesh.addGradient(
      area.AlphaX, area.AlphaY, area.AlphaW, area.AlphaH,
      static_cast<int>(kAlphaSegments), 1, [red, green, blue](float s, float) {
        const int alpha = static_cast<int>(std::lround(s * 255.0f));
        return premultiplied(red, green, blue, alpha);
      });

  // Markers. A black/white sandwich stays readable on every colour.
  const float marker = std::max(2.0f, kMarkerDp * ratio);
  const float hueX =
      area.HueX + std::clamp(Hue / 359.999f, 0.0f, 1.0f) * area.HueW;
  mesh.addQuad(hueX - marker, area.HueY, marker * 2.0f, area.HueH,
               premultiplied(0, 0, 0, 255));
  mesh.addQuad(hueX - marker * 0.5f, area.HueY, marker, area.HueH,
               premultiplied(255, 255, 255, 255));

  const float svX = area.SvX + Saturation * area.SvW;
  const float svY = area.SvY + (1.0f - Value) * area.SvH;
  const float box = marker * 3.0f;
  mesh.addQuad(svX - box * 0.5f, svY - box * 0.5f, box, box,
               premultiplied(0, 0, 0, 255));
  mesh.addQuad(svX - box * 0.5f + 1.0f, svY - box * 0.5f + 1.0f, box - 2.0f,
               box - 2.0f, premultiplied(255, 255, 255, 255));
  mesh.addQuad(svX - box * 0.5f + 2.0f, svY - box * 0.5f + 2.0f, box - 4.0f,
               box - 4.0f, premultiplied(0, 0, 0, 255));

  const float alphaX =
      area.AlphaX + (static_cast<float>(Alpha) / 255.0f) * area.AlphaW;
  mesh.addQuad(alphaX - marker, area.AlphaY, marker * 2.0f, area.AlphaH,
               premultiplied(0, 0, 0, 255));
  mesh.addQuad(alphaX - marker * 0.5f, area.AlphaY, marker, area.AlphaH,
               premultiplied(255, 255, 255, 255));

  if (Disabled)
    mesh.addQuad(0.0f, 0.0f, size.x, size.y, premultiplied(10, 14, 18, 150));

  if (mesh.Vertices.empty())
    return;
  if (Rml::RenderInterface *renderer = Rml::GetRenderInterface())
    Geometry = renderer->CompileGeometry(mesh.Vertices, mesh.Indices);
}

void ElementColorPicker::OnRender() {
  if (!Rml::GetRenderInterface())
    return;
  const Rml::Vector2f size = GetBox().GetSize(Rml::BoxArea::Content);
  if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x <= 0.0f ||
      size.y <= 0.0f)
    return;
  if (!Geometry || GeometryDirty || size != GeometrySize)
    rebuildGeometry(size);
  if (Geometry)
    Rml::GetRenderInterface()->RenderGeometry(
        Geometry, GetAbsoluteOffset(Rml::BoxArea::Content), 0);
}

void registerElement() {
  Rml::Factory::RegisterElementInstancer("colorpicker", &Instancer);
}

} // namespace rmlui::colorpicker
