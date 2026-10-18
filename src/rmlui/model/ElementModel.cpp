// New implementation; see ElementModel.h. Register before loading documents,
// render once at the end-of-frame UI boundary, never inside Rml geometry draws.
#include "ElementModel.h"
#include "../modelpreview/Options.h"
#include "../modelpreview/Target.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <abc/Console.h>
#include <cmath>
#include <vector>
#include <algorithm>

namespace rmlui::model {
namespace {
// Drag sensitivity and limits (degrees per pixel / absolute tilt).
constexpr float kYawPerPixel = 0.7f;
constexpr float kPitchPerPixel = 0.45f;
constexpr float kMaxPitch = 22.0f;

std::vector<ElementModel *> &elements() {
  static std::vector<ElementModel *> Elements;
  return Elements;
}
Rml::ElementInstancerGeneric<ElementModel> Instancer;
} // namespace

ElementModel::ElementModel(const Rml::String &Tag)
    : Element(Tag), Config(std::make_unique<modelpreview::Options>()),
      Target(std::make_unique<modelpreview::Target>()), Listener(this) {
  auto Child = Rml::Factory::InstanceElement(this, "img", "img", {});
  Image = Child.get();
  if (Image) {
    Image->SetProperty("width", "100%");
    Image->SetProperty("height", "100%");
    Image->SetProperty("display", "block");
    Image->SetProperty("pointer-events", "none");
    // FBO pixels have bottom-left origin, Rml images top-left.
    Image->SetProperty("transform", "scaleY(-1)");
    // Nothing is presented before the first FBO lands. An img whose
    // texture is unresolved draws as a filled rectangle (the render
    // interface's untextured fast path), i.e. a white box for a source
    // that cannot load - renderPreview unhides it only when the preview
    // produced pixels.
    Image->SetProperty("visibility", "hidden");
    AppendChild(std::move(Child));
  }
  elements().push_back(this);
}

ElementModel::~ElementModel() {
  std::erase(elements(), this);
}

void ElementModel::OnAttributeChange(const Rml::ElementAttributes &Changed) {
  Element::OnAttributeChange(Changed);
  // Reparse from defaults so RemoveAttribute resets, rather than retaining
  // the removed value. Parsing errors do not leave a stale source visible.
  modelpreview::Options Updated;
  for (const auto &[Name, Value] : GetAttributes()) {
    if (!Updated.set(Name, Value.Get<Rml::String>()))
      abc::ConsoleWarn("[AbcUi model] invalid %s on #%s\n", Name.c_str(),
                       GetId().c_str());
  }
  *Config = std::move(Updated);
}

unsigned int ElementModel::texture() const {
  return Target->texture();
}

unsigned int ElementModel::frames() const {
  return Target->frames();
}

void ElementModel::OnUpdate() {
  Element::OnUpdate();
  // (Re)bind the drag listeners to the owner document: it is only known
  // after the element was appended to a document.
  Rml::ElementDocument *document = GetOwnerDocument();
  if (document == ListenedDocument)
    return;
  if (ListenedDocument) {
    RemoveEventListener("mousedown", &Listener, false);
    ListenedDocument->RemoveEventListener("mousemove", &Listener, false);
    ListenedDocument->RemoveEventListener("mouseup", &Listener, false);
    ListenedDocument = nullptr;
  }
  if (document && Config->DragRotate) {
    // The element's own press starts the drag; the document keeps it alive
    // while the pointer wanders off the box.
    AddEventListener("mousedown", &Listener, false);
    document->AddEventListener("mousemove", &Listener, false);
    document->AddEventListener("mouseup", &Listener, false);
    ListenedDocument = document;
  }
}

void ElementModel::DragListener::ProcessEvent(Rml::Event &event) {
  const Rml::EventId id = event.GetId();
  if (id == Rml::EventId::Mousedown) {
    if (event.GetParameter<int>("button", 0) == 0)
      Owner->beginDrag(event.GetParameter<int>("mouse_x", 0),
                       event.GetParameter<int>("mouse_y", 0));
  } else if (id == Rml::EventId::Mousemove)
    Owner->updateDrag(event.GetParameter<int>("mouse_x", 0),
                      event.GetParameter<int>("mouse_y", 0));
  else if (id == Rml::EventId::Mouseup)
    Owner->endDrag();
}

void ElementModel::beginDrag(int x, int y) {
  if (!Config->DragRotate)
    return;
  Dragging = true;
  DragStartX = x;
  DragStartY = y;
  SetProperty("cursor", "move");
}

void ElementModel::updateDrag(int x, int y) {
  if (!Dragging)
    return;
  const float yaw = static_cast<float>(x - DragStartX) * kYawPerPixel;
  const float pitch = static_cast<float>(y - DragStartY) * kPitchPerPixel;
  DragYaw = yaw;
  DragPitch = std::clamp(pitch, -kMaxPitch, kMaxPitch);
}

void ElementModel::endDrag() {
  if (!Dragging)
    return;
  Dragging = false;
  SetProperty("cursor", "default");
}

bool ElementModel::renderPreview() {
  if (!Image || !GetContext() || !IsVisible(true))
    return false;
  const auto Size = GetBox().GetSize(Rml::BoxArea::Content);
  if (!std::isfinite(Size.x) || !std::isfinite(Size.y) || Size.x <= 0 ||
      Size.y <= 0)
    return false;
  // The drag offsets ride the entity's angles: the camera keeps its framing
  // and the model turns in place (an inspection gesture, not a camera move).
  modelpreview::Options Effective = *Config;
  if (Config->DragRotate) {
    Effective.Angles[1] = Config->Angles[1] + DragYaw;
    Effective.Angles[0] = Config->Angles[0] + DragPitch;
  }
  const bool Ready = Target->render(
      Effective, static_cast<int>(std::min(std::ceil(Size.x), 2048.0f)),
      static_cast<int>(std::min(std::ceil(Size.y), 2048.0f)));
  if (ImageReady == Ready)
    return false;
  ImageReady = Ready;
  if (Ready) {
    Image->SetAttribute("src", Target->uri());
    Image->SetProperty("visibility", "visible");
  } else {
    Image->SetProperty("visibility", "hidden");
  }
  return true;
}

void registerElement() {
  Rml::Factory::RegisterElementInstancer("model", &Instancer);
}

void renderPreviews() {
  Rml::Context *Context = nullptr;
  for (auto *Element : elements())
    if (Element->renderPreview())
      Context = Element->GetContext();
  if (Context)
    Context->Update();
}
} // namespace rmlui::model
