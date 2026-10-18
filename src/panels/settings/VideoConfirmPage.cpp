// panels::settings::VideoConfirmPage implementation - see the header.
#include "VideoConfirmPage.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>

#include <abc/Format.h>

#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"
#include "../../services/settings/VideoModeModel.h"

namespace panels {
namespace settings {

namespace {

constexpr const char *kActionKeep = "video-keep";
constexpr const char *kActionRevert = "video-revert";

std::string translated(const char *token) {
  std::string out;
  if (token && *token == '#') {
    if (!framework::Localization::translate(out, token + 1))
      out = token;
  } else if (token) {
    out = token;
  }
  return out;
}

} // namespace

VideoConfirmPage::VideoConfirmPage()
    : InteractiveDocument("videoconfirm",
                          "abc/abcui/gameui/rml/videoconfirm.rml", true),
      Listener(this) {}

VideoConfirmPage::~VideoConfirmPage() {
  close();
}

void VideoConfirmPage::onDocumentReady() {
  if (Rml::ElementDocument *doc = document())
    addListener(doc, "click", &Listener);
  render();
}

void VideoConfirmPage::setText(const char *id, const std::string &text) {
  if (!document())
    return;
  if (Rml::Element *node = document()->GetElementById(id))
    node->SetInnerRML(framework::TextUtil::escapeRml(text));
}

void VideoConfirmPage::render() {
  if (!document() || !Model)
    return;
  using Kind = services::settings::VideoModeModel::ConfirmKind;
  const Kind kind = Model->confirmKind(); // re-queries the live actual mode

  // The headline states the mode the engine ACTUALLY runs, plus what the
  // engine was asked to switch to (the request alone proves nothing).
  const bool actualKnown = Model->actualKnown();
  std::string actualText =
      actualKnown
          ? services::settings::VideoModeModel::describe(Model->actual())
          : translated("#VideoConfirm_Unknown");
  setText("videoconfirm-actual", actualText);
  const auto &record = Model->pending();
  services::settings::VideoMode actual;
  actual.Width = record.NewWidth;
  actual.Height = record.NewHeight;
  actual.Windowed = record.NewWindowed != 0;
  setText("videoconfirm-requested",
          services::settings::VideoModeModel::describe(actual));

  switch (kind) {
  case Kind::kApplied:
    setText("videoconfirm-lead", translated("#VideoConfirm_Lead"));
    break;
  case Kind::kNotApplied:
    setText("videoconfirm-lead", translated("#VideoConfirm_NotApplied"));
    break;
  case Kind::kUnknown:
    setText("videoconfirm-lead", translated("#VideoConfirm_Unknown"));
    break;
  }
  setText("videoconfirm-timeout",
          abc::Format(translated("#VideoConfirm_TimeoutTime"),
                      std::to_string(Model->remainingSeconds())));

  // The Keep entry is only offered when the switch actually took effect;
  // the model refuses anything else anyway (fail-closed projection).
  if (Rml::Element *keep = document()->GetElementById("videoconfirm-keep")) {
    keep->SetClass("video-disabled", kind != Kind::kApplied);
    if (Rml::ElementFormControl *form =
            rmlui_dynamic_cast<Rml::ElementFormControl *>(keep))
      form->SetDisabled(kind != Kind::kApplied);
  }
}

void VideoConfirmPage::refocusDefault() {
  if (!document())
    return;
  if (Rml::Element *focus = document()->GetElementById("videoconfirm-revert"))
    focus->Focus();
}

void VideoConfirmPage::handleClick(Rml::Event &event) {
  Rml::Element *node = event.GetTargetElement();
  while (node && !node->HasAttribute("data-action"))
    node = node->GetParentNode();
  if (!node)
    return;
  const Rml::String action = node->GetAttribute<Rml::String>("data-action", "");
  const ActionSink sink = Sink;
  if (!sink)
    return;
  if (action == kActionKeep)
    sink(kActionKeep);
  else if (action == kActionRevert)
    sink(kActionRevert);
  // Unknown actions are dropped: the page only binds its own names.
}

void VideoConfirmPage::PageListener::ProcessEvent(Rml::Event &event) {
  Owner->handleClick(event);
}

} // namespace settings
} // namespace panels
