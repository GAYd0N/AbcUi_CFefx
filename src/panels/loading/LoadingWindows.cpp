// panels::loading implementation - see LoadingWindows.h.
#include "LoadingWindows.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementProgress.h>
#include <RmlUi/Core/Event.h>

#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"
#include "../../rmlui/VGUI2ExtensionImport.h"
#include "../../services/loading/LoadingModel.h"

#include <cstdio>
#include <cstring>

namespace panels::loading {

namespace {

constexpr const char *kActionCancel = "loading-cancel";
constexpr const char *kActionRetry = "error-retry";
constexpr const char *kActionReturn = "error-return";
constexpr const char *kActionPasswordConnect = "password-connect";
constexpr const char *kActionPasswordCancel = "password-cancel";
constexpr const char *kActionDirectConnect = "directconnect-connect";
constexpr const char *kActionDirectCancel = "directconnect-cancel";

Rml::Element *actionNode(Rml::Element *node) {
  for (Rml::Element *it = node; it; it = it->GetParentNode()) {
    if (it->HasAttribute("data-action"))
      return it;
  }
  return nullptr;
}

// Engine-text resolution for the loading stream: our own table first (a
// project token), then the engine's own ILocalize (the "#GameUI_..." tokens
// the shipped dialog resolves the same way), then the raw text (honest
// fallback - never silently blanked).
std::string resolveText(const std::string &text) {
  if (text.empty())
    return {};
  if (text[0] != '#')
    return text;
  std::string out;
  if (framework::Localization::translate(out, text.substr(1)))
    return out;
  out = vgui2ext::localizeToken(text.c_str());
  if (!out.empty())
    return out;
  return text;
}

void setText(Rml::ElementDocument *doc, const char *id,
             const std::string &text) {
  if (!doc)
    return;
  if (Rml::Element *node = doc->GetElementById(id))
    node->SetInnerRML(framework::TextUtil::escapeRml(text));
}

void setVisible(Rml::ElementDocument *doc, const char *id, bool visible) {
  if (!doc)
    return;
  if (Rml::Element *node = doc->GetElementById(id))
    node->SetProperty("display", visible ? "block" : "none");
}

std::string percentText(float fraction) {
  if (fraction < 0.0f)
    fraction = 0.0f;
  if (fraction > 1.0f)
    fraction = 1.0f;
  char buffer[16];
  _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%d%%",
              static_cast<int>(fraction * 100.0f + 0.5f));
  return buffer;
}

} // namespace

LoadingPage::LoadingPage()
    : InteractiveDocument("menuloading", "abc/abcui/gameui/rml/loading.rml",
                          true),
      Listener(this) {}

LoadingPage::~LoadingPage() {
  close();
}

void LoadingPage::onDocumentReady() {
  if (Rml::ElementDocument *doc = document())
    addListener(doc, "click", &Listener);
  render();
}

void LoadingPage::render() {
  Rml::ElementDocument *doc = document();
  if (!doc || !Model)
    return;
  // Title: the attempt-kind token when the module set one (the T19 local
  // start), else the shipped connect title (the document's own text node).
  if (!TitleToken.empty())
    setText(doc, "loading-title", resolveText(TitleToken));
  // Status line: the engine truth when it has spoken, else the page's own
  // "connecting" default (the few frames before the first callback).
  const std::string status = Model->statusText().empty()
                                 ? resolveText("#Loading_Connecting")
                                 : resolveText(Model->statusText());
  setText(doc, "loading-status", status);
  setText(doc, "loading-target", Model->address());

  const std::string resource = resolveText(Model->resourceText());
  setText(doc, "loading-resource", resource);
  setVisible(doc, "loading-resource", !resource.empty());

  // Primary channel: honest unknown stays hidden (no fake fill).
  const auto &primary = Model->primary();
  setVisible(doc, "loading-progress", primary.Known);
  setVisible(doc, "loading-progress-text", primary.Known);
  if (primary.Known) {
    float fraction = primary.Fraction;
    if (fraction <= 0.0f && primary.Steps > 0)
      fraction =
          static_cast<float>(primary.Point) / static_cast<float>(primary.Steps);
    if (Rml::ElementProgress *bar = rmlui_dynamic_cast<Rml::ElementProgress *>(
            doc->GetElementById("loading-progress")))
      bar->SetValue(fraction);
    setText(doc, "loading-progress-text", percentText(fraction));
  }

  // Secondary channel: shows only once the engine drives it.
  const bool secondaryShown =
      Model->secondaryFraction() > 0.0f || !Model->secondaryText().empty();
  setVisible(doc, "loading-secondary", secondaryShown);
  setVisible(doc, "loading-secondary-text", secondaryShown);
  if (secondaryShown) {
    if (Rml::ElementProgress *bar = rmlui_dynamic_cast<Rml::ElementProgress *>(
            doc->GetElementById("loading-secondary")))
      bar->SetValue(Model->secondaryFraction());
    const std::string text = Model->secondaryText().empty()
                                 ? percentText(Model->secondaryFraction())
                                 : resolveText(Model->secondaryText());
    setText(doc, "loading-secondary-text", text);
  }

  // The cancel entry reflects the model's once-only latch: the module (and
  // the backend generation latch behind it) guarantees one termination per
  // attempt; the visual state makes the one-frame double-click window
  // honest. RmlUi buttons are not ElementFormControl - the disabled look
  // rides a style class like the video-confirm page.
  if (Rml::Element *cancel = doc->GetElementById("loading-cancel"))
    cancel->SetClass("loading-disabled", Model->canceled());
}

void LoadingPage::handle(Rml::Event &event) {
  Rml::Element *entry = actionNode(event.GetTargetElement());
  if (!entry)
    return;
  if (entry->GetAttribute<Rml::String>("data-action", "") != kActionCancel)
    return;
  const ActionSink Action = Sink;
  if (Action)
    Action(kActionCancel);
}

void LoadingPage::WindowListener::ProcessEvent(Rml::Event &event) {
  Owner->handle(event);
}

void LoadingPage::refocusDefault() {
  if (Rml::Element *fallback =
          document() ? document()->GetElementById("loading-cancel") : nullptr)
    fallback->Focus();
}

ConnectionErrorOverlay::ConnectionErrorOverlay()
    : InteractiveDocument("menuconnerror",
                          "abc/abcui/gameui/rml/connerror.rml"),
      Listener(this) {}

ConnectionErrorOverlay::~ConnectionErrorOverlay() {
  close();
}

void ConnectionErrorOverlay::onDocumentReady() {
  if (Rml::ElementDocument *doc = document())
    addListener(doc, "click", &Listener);
}

void ConnectionErrorOverlay::showFor(const char *reason) {
  Rml::ElementDocument *doc = document();
  if (!doc)
    return;
  const std::string text = (reason && *reason)
                               ? resolveText(reason)
                               : resolveText("#Loading_ErrorGeneric");
  setText(doc, "connerror-reason", text);
}

void ConnectionErrorOverlay::handle(Rml::Event &event) {
  Rml::Element *entry = actionNode(event.GetTargetElement());
  if (!entry)
    return;
  const Rml::String action =
      entry->GetAttribute<Rml::String>("data-action", "");
  if (action != kActionRetry && action != kActionReturn)
    return;
  const ActionSink Action = Sink;
  if (Action)
    Action(action.c_str());
}

void ConnectionErrorOverlay::WindowListener::ProcessEvent(Rml::Event &event) {
  Owner->handle(event);
}

void ConnectionErrorOverlay::refocusDefault() {
  if (Rml::Element *fallback =
          document() ? document()->GetElementById("connerror-return") : nullptr)
    fallback->Focus();
}

PasswordOverlay::PasswordOverlay()
    : InteractiveDocument("menupassword", "abc/abcui/gameui/rml/password.rml"),
      Listener(this) {}

PasswordOverlay::~PasswordOverlay() {
  close();
}

void PasswordOverlay::onDocumentReady() {
  if (Rml::ElementDocument *doc = document())
    addListener(doc, "click", &Listener);
}

void PasswordOverlay::prepare(bool invalid) {
  Rml::ElementDocument *doc = document();
  if (!doc)
    return;
  setText(doc, "password-lead",
          resolveText(invalid ? "#Loading_PasswordInvalid"
                              : "#Loading_PasswordRequired"));
  setText(doc, "password-hint", "");
  if (Rml::ElementFormControl *field =
          rmlui_dynamic_cast<Rml::ElementFormControl *>(
              doc->GetElementById("password-field")))
    field->SetValue("");
}

// showHint is shared by the two input overlays below; declared here as a small
// file-local helper taking the document.
namespace {
void clearOrSetHint(Rml::ElementDocument *doc, const char *id,
                    const char *text) {
  setText(doc, id, text ? resolveText(text) : std::string());
}
} // namespace

std::string PasswordOverlay::value() const {
  if (!document())
    return {};
  if (Rml::ElementFormControl *field =
          rmlui_dynamic_cast<Rml::ElementFormControl *>(
              document()->GetElementById("password-field")))
    return field->GetValue();
  return {};
}

Rml::Element *PasswordOverlay::editor() const {
  return document() ? document()->GetElementById("password-field") : nullptr;
}

void PasswordOverlay::handle(Rml::Event &event) {
  Rml::Element *entry = actionNode(event.GetTargetElement());
  if (!entry)
    return;
  const Rml::String action =
      entry->GetAttribute<Rml::String>("data-action", "");
  if (action != kActionPasswordConnect && action != kActionPasswordCancel)
    return;
  const ActionSink Action = Sink;
  if (Action)
    Action(action.c_str());
}

void PasswordOverlay::WindowListener::ProcessEvent(Rml::Event &event) {
  Owner->handle(event);
}

void PasswordOverlay::refocusDefault() {
  if (Rml::Element *field =
          document() ? document()->GetElementById("password-field") : nullptr)
    field->Focus();
}

DirectConnectOverlay::DirectConnectOverlay()
    : InteractiveDocument("menudirectconnect",
                          "abc/abcui/gameui/rml/directconnect.rml"),
      Listener(this) {}

DirectConnectOverlay::~DirectConnectOverlay() {
  close();
}

void DirectConnectOverlay::onDocumentReady() {
  if (Rml::ElementDocument *doc = document())
    addListener(doc, "click", &Listener);
}

void DirectConnectOverlay::prepare() {
  Rml::ElementDocument *doc = document();
  if (!doc)
    return;
  if (Rml::ElementFormControl *field =
          rmlui_dynamic_cast<Rml::ElementFormControl *>(
              doc->GetElementById("directconnect-field")))
    field->SetValue("");
  showHint("");
}

std::string DirectConnectOverlay::value() const {
  if (!document())
    return {};
  if (Rml::ElementFormControl *field =
          rmlui_dynamic_cast<Rml::ElementFormControl *>(
              document()->GetElementById("directconnect-field")))
    return field->GetValue();
  return {};
}

Rml::Element *DirectConnectOverlay::editor() const {
  return document() ? document()->GetElementById("directconnect-field")
                    : nullptr;
}

void DirectConnectOverlay::showHint(const char *text) {
  Rml::ElementDocument *doc = document();
  if (!doc)
    return;
  if (text && *text)
    clearOrSetHint(doc, "directconnect-hint", text);
  else
    setText(doc, "directconnect-hint", "");
}

void DirectConnectOverlay::handle(Rml::Event &event) {
  Rml::Element *entry = actionNode(event.GetTargetElement());
  if (!entry)
    return;
  const Rml::String action =
      entry->GetAttribute<Rml::String>("data-action", "");
  if (action != kActionDirectConnect && action != kActionDirectCancel)
    return;
  const ActionSink Action = Sink;
  if (Action)
    Action(action.c_str());
}

void DirectConnectOverlay::WindowListener::ProcessEvent(Rml::Event &event) {
  Owner->handle(event);
}

void DirectConnectOverlay::refocusDefault() {
  if (Rml::Element *field =
          document() ? document()->GetElementById("directconnect-field")
                     : nullptr)
    field->Focus();
}

} // namespace panels::loading
