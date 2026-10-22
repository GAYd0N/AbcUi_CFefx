#pragma once

// New custom Rml element. Owns a Target; an internal <img> handles clipping,
// transforms and opacity through the ordinary Rml renderer.
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/EventListener.h>

#include <memory>

namespace Rml {
class ElementDocument;
class Event;
} // namespace Rml

namespace modelpreview {
struct Options;
class Target;
} // namespace modelpreview

namespace rmlui::model {
class ElementModel : public Rml::Element {
public:
  RMLUI_RTTI_DeclareWithParent(ElementModel, Rml::Element)

      explicit ElementModel(const Rml::String &Tag);
  ~ElementModel() override;
  bool renderPreview();
  unsigned int texture() const;
  unsigned int frames() const;

protected:
  void OnAttributeChange(const Rml::ElementAttributes &Changed) override;
  void OnUpdate() override;

private:
  // Pointer drag rotation (settings' multiplayer preview): while the primary
  // button is held over the element, horizontal motion turns the model and
  // vertical motion tilts it (clamped) - the CS:GO-style inspection gesture.
  void beginDrag(int x, int y);
  void updateDrag(int x, int y);
  void endDrag();
  // Document-scoped move/release listeners: a drag must survive the pointer
  // leaving the element box (Rml only dispatches mousemove to the hover
  // chain), so the element listens on its owner document while dragging.
  class DragListener final : public Rml::EventListener {
  public:
    explicit DragListener(ElementModel *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    ElementModel *Owner;
  };

  // PIMPL-ish: the element header must not pull the service headers in.
  std::unique_ptr<modelpreview::Options> Config;
  std::unique_ptr<modelpreview::Target> Target;
  DragListener Listener;
  Rml::Element *Image = nullptr;
  Rml::ElementDocument *ListenedDocument = nullptr;
  bool ImageReady = false;
  bool Dragging = false;
  int DragStartX = 0;
  int DragStartY = 0;
  float DragYaw = 0.0f;
  float DragPitch = 0.0f;
  // The config's own angles are the base; the drag adds to them.
  float BaseYaw = 0.0f;
  float BasePitch = 0.0f;
};

void registerElement();
void renderPreviews();
} // namespace rmlui::model
