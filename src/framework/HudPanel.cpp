// framework::HudPanel implementation - see HudPanel.h.
#include "HudPanel.h"
#include "../data/HudState.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>

#include <abc/Console.h>

namespace framework {

HudPanel::HudPanel(const char *name, const char *documentPath, int zIndex)
    : Name(name), DocumentPath(documentPath), ZIndex(zIndex) {}

HudPanel::~HudPanel() {
  for (auto &cleanup : Cleanups)
    cleanup();
  if (Document)
    Document->Close();
}

bool HudPanel::loadDocument(Rml::Context *context) {
  if (!context || Document)
    return Document != nullptr;
  Document = context->LoadDocument(DocumentPath);
  if (!Document) {
    abc::ConsoleWarn("[AbcUi] panel \"%s\": failed to load %s\n", Name.c_str(),
                     DocumentPath.c_str());
    return false;
  }
  // Passive HUD documents must not intercept scoreboard/MOTD hit testing.
  if (Name != "scoreboard" && Name != "motd")
    Document->SetProperty("pointer-events", "none");
  // Cross-document paint order (the radar parks itself under the readouts).
  if (ZIndex != 0)
    Document->SetProperty("z-index", std::to_string(ZIndex));
  // Start hidden; panels opt into visibility via show() (data-driven).
  Document->Hide();
  onDocumentReady();
  return true;
}

void HudPanel::show() {
  // Idempotent: RmlUi's Show() defaults to FocusFlag::Auto, so re-showing a
  // visible document would silently steal the context focus (key input of
  // whatever document the user is interacting with, e.g. an open TextMenu).
  if (hudstate::isHudHidden(hudstate::kHideAll) && Name != "scoreboard" &&
      Name != "motd" && Name != "textmenu" && Name != "vote")
    return;
  WantsVisible = true;
  if (SessionActive && Document && !Document->IsVisible())
    Document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

void HudPanel::hide() {
  WantsVisible = false;
  if (Document)
    Document->Hide();
}

void HudPanel::setSessionActive(bool active) {
  SessionActive = active;
  if (!Document)
    return;
  if (!active)
    Document->Hide();
  else if (WantsVisible)
    show();
}

bool HudPanel::isVisible() const {
  return Document && Document->IsVisible();
}

void HudPanel::addCleanup(std::function<void()> fn) {
  Cleanups.push_back(std::move(fn));
}

Rml::Element *HudPanel::element(const char *id) {
  Rml::Element *found = Document ? Document->GetElementById(id) : nullptr;
  if (!found)
    abc::ConsoleWarn("[AbcUi] panel \"%s\": element \"%s\" missing\n",
                     Name.c_str(), id);
  return found;
}

} // namespace framework
