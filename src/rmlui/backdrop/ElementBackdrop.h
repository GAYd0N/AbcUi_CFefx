#pragma once

// rmlui::backdrop::ElementBackdrop - the <backdrop> Rml element: one
// full-window image showing services::backdrop::Backdrop's blurred frame,
// plus an optional darkening layer for legibility.
//
// Every menu document carries exactly one (framework::attachMenuBackdrop does
// the insertion), sized to the Rml context in pixels - not by percentage rules
// - so a page whose body collapsed to 0x0 (the scoreboard lesson) still gets a
// window-covering backdrop.
//
// Attributes:
//   mode  auto|live|video|solid  (default auto: live in a level, video outside)
//   blur  0..4 gaussian rounds   (default 2; 0 = no blur, the raw capture)
//   dim   0..1 darkening opacity (default 0.35; the content panels sit on top)

#include <RmlUi/Core/Element.h>

namespace rmlui::backdrop {

// Debug diagnostics (abc_ui_probe): what the backdrop looks like right now.
struct ProbeInfo {
  int Elements = 0;
  int Visible = 0;
  bool Published = false;
  // Rml::String members (not borrowed pointers): the probe reads them after
  // the owning objects may have moved their caches.
  Rml::String Uri;
  // First visible element's layout facts (empty when none is visible).
  Rml::String Document;
  Rml::String ImageSrc;
  Rml::String ImageVisibility;
  float Width = 0.0f;
  float Height = 0.0f;
  float ImageWidth = 0.0f;
  float ImageHeight = 0.0f;
};

class ElementBackdrop : public Rml::Element {
public:
  explicit ElementBackdrop(const Rml::String &Tag);
  ~ElementBackdrop() override;

  // Mirrors the shared backdrop state into this element (change-gated: a
  // steady backdrop writes no DOM at all).
  bool sync();
  // Drops the image source (the shared backdrop's GPU targets went away).
  void dropImageSource();
  // Debug diagnostics (abc_ui_probe): fills the element-side layout facts.
  void fillProbeInfo(ProbeInfo &info);

protected:
  void OnAttributeChange(const Rml::ElementAttributes &Changed) override;

private:
  Rml::Element *Image = nullptr;
  Rml::Element *Dimmer = nullptr;
  // The document's explicit `blur` attribute; -1 = none, so the
  // abc_ui_menu_backdrop_blur cvar owns the value (and keeps owning it when
  // the user changes it in the settings).
  int Rounds = -1;
  // Last cvar value pushed to the shared backdrop, so a steady cvar costs
  // one compare and a user edit takes effect without a restart.
  int AppliedCvarRounds = -1;
  float Dim = -1.0f;
  float AppliedDim = -1.0f;
  // Last video path pushed from the abc_ui_menu_video cvar: a user edit takes
  // effect without a restart, a steady value costs one string compare.
  std::string AppliedVideoCvar;
};

void registerElement();
// True when at least one backdrop element is actually on screen: the shared
// backdrop only decodes and blurs while a menu document is up.
bool anyVisible();
// Diagnostics for abc_ui_probe (see ProbeInfo).
ProbeInfo probeInfo();
// One sync pass over every live backdrop element; called on the render-end
// beat right after Backdrop::render(). Returns true when the DOM changed.
bool syncAll();
// Drops the shared backdrop's GPU targets (vid restart).
void invalidateTargets();

// Inserts a <backdrop> as the document's first child (painted behind every
// other node) unless the document already declares one. Returns the element.
Rml::Element *attachToDocument(Rml::ElementDocument *document);

} // namespace rmlui::backdrop
