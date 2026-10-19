// panels::menushared implementation - see MenuShellWindows.h.
#include "MenuShellWindows.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Event.h>

#include "../../framework/TextUtil.h"

namespace panels::menushared {

namespace {

// The only two actions the confirm window reports; the state machine's
// quitResolve decides what a confirmation means (exactly one quit).
constexpr const char *kActionConfirm = "quit-yes";
constexpr const char *kActionCancel = "quit-no";
// The stub's only entry: back through the controller.
constexpr const char *kActionReturn = "return";

Rml::Element *actionNode(Rml::Element *node) {
  for (Rml::Element *it = node; it; it = it->GetParentNode()) {
    if (it->HasAttribute("data-action"))
      return it;
  }
  return nullptr;
}

} // namespace

ConfirmQuitOverlay::ConfirmQuitOverlay()
    : InteractiveDocument("menuconfirm",
                          "abc/abcui/gameui/rml/confirmquit.rml", false,
                          kConfirmQuitZIndex),
      Listener(this) {}

ConfirmQuitOverlay::~ConfirmQuitOverlay() {
  close();
}

void ConfirmQuitOverlay::onDocumentReady() {
  // Both buttons, one delegated click binding; the safe default focus sits
  // on the cancel entry via the document's autofocus attribute (Show with
  // FocusFlag::Auto), so Enter cannot confirm a quit by accident.
  if (Rml::ElementDocument *doc = document())
    addListener(doc, "click", &Listener);
}

void ConfirmQuitOverlay::handle(Rml::Event &event) {
  Rml::Element *entry = actionNode(event.GetTargetElement());
  if (!entry)
    return;
  const Rml::String action =
      entry->GetAttribute<Rml::String>("data-action", "");
  if (action != kActionConfirm && action != kActionCancel)
    return;
  const ActionSink Action = Sink; // may replace itself or destroy this page
  if (Action)
    Action(action.c_str());
}

void ConfirmQuitOverlay::WindowListener::ProcessEvent(Rml::Event &event) {
  Owner->handle(event);
}

void ConfirmQuitOverlay::refocusDefault() {
  if (Rml::Element *fallback =
          document() ? document()->GetElementById("confirm-no") : nullptr)
    fallback->Focus();
}

MenuStubPage::MenuStubPage(const char *name, const char *documentPath)
    : InteractiveDocument(name, documentPath, true), Listener(this) {}

MenuStubPage::~MenuStubPage() {
  close();
}

void MenuStubPage::onDocumentReady() {
  if (Rml::ElementDocument *doc = document())
    addListener(doc, "click", &Listener);
}

void MenuStubPage::showFor(const char *title, const char *body) {
  Rml::ElementDocument *doc = document();
  if (!doc)
    return;
  if (Rml::Element *node = doc->GetElementById("stub-title"))
    node->SetInnerRML(framework::TextUtil::escapeRml(title ? title : ""));
  if (Rml::Element *node = doc->GetElementById("stub-body"))
    node->SetInnerRML(framework::TextUtil::escapeRml(body ? body : ""));
}

void MenuStubPage::handle(Rml::Event &event) {
  Rml::Element *entry = actionNode(event.GetTargetElement());
  if (!entry)
    return;
  const Rml::String action =
      entry->GetAttribute<Rml::String>("data-action", "");
  if (action != kActionReturn)
    return;
  const ActionSink Action = Sink;
  if (Action)
    Action(action.c_str());
}

void MenuStubPage::WindowListener::ProcessEvent(Rml::Event &event) {
  Owner->handle(event);
}

void MenuStubPage::refocusDefault() {
  if (Rml::Element *fallback =
          document() ? document()->GetElementById("stub-return") : nullptr)
    fallback->Focus();
}

} // namespace panels::menushared
