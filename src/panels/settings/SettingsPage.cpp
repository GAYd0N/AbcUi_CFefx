// Extracted from SettingsPage.cpp (UTF-8). Responsibility: SettingsPage.
#include "panels/settings/SettingsUi.h"
#include "panels/settings/SettingsPage.h"
#include "panels/settings/sections/VoiceSection.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include "services/settings/SettingsModel.h"
#include "services/settings/KeyBindModel.h"
#include "services/settings/VideoModeModel.h"
#include "services/settings/VoiceModel.h"
#include "framework/TextUtil.h"
#include <abc/Format.h>
#include <string>
namespace panels::settings {
using namespace detail;
SettingsPage::SettingsPage()
    : InteractiveDocument("menusettings", "abc/abcui/gameui/rml/settings.rml",
                          true),
      Listener(this) {}

SettingsPage::~SettingsPage() {
  releasePreviews();
  close();
}

void SettingsPage::onDocumentReady() {
  VideoView.reset();
  SelectValuesShown.clear();
  HueSwatchShown.clear();
  if (Rml::ElementDocument *doc = document()) {
    // Document-level delegation for both: row text fields commit through
    // the data-row ancestor lookup, the T12 resolution select through its
    // id (a #rows-scoped change listener never sees the video section -
    // live-caught by the offline page test).
    addListener(doc, "click", &Listener);
    addListener(doc, "change", &Listener);
    // The legacy all-in-one projection (no page definitions - the model
    // fixtures) has no cards to claim widgets: show the parking host so
    // the bind list and hardware blocks stay reachable.
    if (!Pages) {
      if (Rml::Element *host = doc->GetElementById("settings-widget-host"))
        host->SetProperty("display", "block");
    }
  }
  buildRows();
  buildBindRows();
  buildNavigation();
  render();
}

void SettingsPage::render(const char *skipInputRow) {
  if (!document() || InRender_)
    return;
  InRender_ = true;
  if (Model) {
    for (const auto &view : Model->rows())
      renderRow(view,
                skipInputRow && std::string(view.row->Cvar) == skipInputRow);
  }
  if (BindModel) {
    for (const auto &view : BindModel->rows())
      renderBindRow(view);
    renderBindConflict();
  }
  // Always projected: with no model (or no query interface) the section
  // renders its fail-closed state instead of staying silently empty.
  renderVideoSection();
  renderVoiceSection();
  renderPreviews();
  renderCrosshairPreview();
  renderSprayMaker();
  setStatusSummary();
  setBindStatusSummary();
  InRender_ = false;
}

void SettingsPage::renderVideoSection() {
  VideoView.render(document(), VideoModel);
}

void SettingsPage::renderVoiceSection() {
  sections::renderVoice(document(), VoiceModel_, VoiceVolumeSkipField);
}

void SettingsPage::applyAndReport() {
  if (!document())
    return;
  int applied = 0, failed = 0, queued = 0;
  if (Model) {
    const auto results = Model->apply();
    for (const auto &result : results) {
      switch (result.outcome) {
      case services::settings::ApplyOutcome::kApplied:
        ++applied;
        break;
      case services::settings::ApplyOutcome::kQueued:
        ++queued;
        break;
      default:
        ++failed;
        break;
      }
    }
  }
  if (BindModel) {
    // Bind writes ride the engine command buffer: kApplied rows already
    // match the truth, kQueued rows resolve on the next frames (the
    // module verifies per tick and re-renders on change).
    BindModel->apply();
  }
  bool voiceApplied = true;
  if (VoiceModel_ && VoiceModel_->available() && VoiceModel_->isDirty()) {
    // Per-control apply like the model rows: each control reports its
    // own outcome, a refused device write keeps the draft.
    voiceApplied = VoiceModel_->applyMicVolume();
    voiceApplied = VoiceModel_->applyMicBoost() && voiceApplied;
    if (voiceApplied)
      ++applied;
    else
      ++failed;
  }
  if (Rml::Element *status = document()->GetElementById("settings-status")) {
    const int touched =
        applied + failed + queued + (BindModel && BindModel->isDirty() ? 1 : 0);
    std::string text;
    if (touched == 0) {
      text = translated("#Settings_StatusClean");
    } else if (failed == 0) {
      text = translated("#Settings_Applied");
      if (queued > 0)
        text += " | " + abc::Format("{0} {1}", std::to_string(queued),
                                    translated("#Settings_QueuedPlural"));
    } else {
      text = abc::Format("{0}/{1} {2}", std::to_string(applied),
                         std::to_string(applied + failed),
                         translated("#Settings_ApplyPartial"));
    }
    status->SetInnerRML(framework::TextUtil::escapeRml(text));
  }
  render();
}

bool SettingsPage::isDirty() const {
  const bool settingsDirty = Model && Model->isDirty();
  const bool bindsDirty = BindModel && BindModel->isDirty();
  const bool voiceDirty =
      VoiceModel_ && VoiceModel_->available() && VoiceModel_->isDirty();
  // A staged display change is a pending write too (the module applies it
  // through the restart-driving intent): leaving must prompt.
  const bool videoDirty = VideoModel && VideoModel->hasStaged();
  return settingsDirty || bindsDirty || videoDirty || voiceDirty;
}

void SettingsPage::requestLeave() {
  if (!Model)
    return;
  // A picker in flight consumes the leave intent: ESC/return closes the
  // browser, never the whole page (the layer is the top-most surface).
  if (Spray.browsing()) {
    closeSprayBrowser();
    return;
  }
  if (!isDirty()) { // both models: a pending bind draft also blocks a silent
                    // exit
    const ActionSink action = Sink;
    if (action)
      action(kActionReturn);
    return;
  }
  if (ActiveLayer == kLayerLeave) {
    hideLayers(); // second request: dismissing the prompt is "stay"
    return;
  }
  showLayer(kLayerLeave);
}

void SettingsPage::showLayer(const char *id) {
  Rml::ElementDocument *doc = document();
  if (!doc)
    return;
  hideLayers();
  if (Rml::Element *layer = doc->GetElementById(id)) {
    layer->SetProperty("display", "block");
    ActiveLayer = id;
  }
  // autofocus only rides the document Show; a layer opened later takes its
  // safe default explicitly (cancel/stay, never the destructive entry).
  if (Rml::Element *focus = doc->GetElementById(safeFocusForLayer(id)))
    focus->Focus();
}

void SettingsPage::hideLayers() {
  Rml::ElementDocument *doc = document();
  if (!doc)
    return;
  for (const char *id : {kLayerReset, kLayerLeave, kLayerBindConflict,
                         kLayerBindReset, kLayerSprayFile}) {
    if (Rml::Element *layer = doc->GetElementById(id))
      layer->SetProperty("display", "none");
  }
  ActiveLayer.clear();
  if (Spray.browsing())
    Spray.close(doc);
}

void SettingsPage::openSprayBrowser() {
  Rml::ElementDocument *doc = document();
  if (!doc)
    return;
  hideLayers();
  Spray.open(doc);
  if (Rml::Element *layer = doc->GetElementById(kLayerSprayFile)) {
    layer->SetProperty("display", "block");
    ActiveLayer = kLayerSprayFile;
  }
  if (Rml::Element *focus =
          doc->GetElementById(safeFocusForLayer(kLayerSprayFile)))
    focus->Focus();
}

void SettingsPage::closeSprayBrowser() {
  hideLayers();
}

void SettingsPage::setSprayResult(bool ok, const std::string &message,
                                  const std::string &previewUri) {
  Spray.setResult(ok, message);
  Spray.setPreviewUri(previewUri);
  hideLayers();
  render();
}

void SettingsPage::renderSprayMaker() {
  Rml::ElementDocument *doc = document();
  if (!doc)
    return;
  if (Rml::Element *status = doc->GetElementById("spray-maker-status"))
    status->SetInnerRML(
        framework::TextUtil::escapeRml(translated(Spray.statusText().c_str())));
  if (Rml::Element *image = doc->GetElementById("spray-made-preview")) {
    if (!Spray.hasPreview()) {
      image->SetProperty("display", "none");
      return;
    }
    const std::string &uri = Spray.previewUri();
    if (image->GetAttribute<Rml::String>("src", "") != uri)
      image->SetAttribute("src", uri);
    image->SetProperty("display", "block");
  }
}

void SettingsPage::refocusDefault() {
  Rml::ElementDocument *doc = document();
  if (!doc)
    return;
  if (!ActiveLayer.empty()) {
    if (Rml::Element *focus =
            doc->GetElementById(safeFocusForLayer(ActiveLayer.c_str())))
      focus->Focus();
    return;
  }
  if (Rml::Element *focus = doc->GetElementById("settings-apply"))
    focus->Focus();
}

void SettingsPage::PageListener::ProcessEvent(Rml::Event &event) {
  if (event.GetType() == "change")
    Owner->handleChange(event);
  else
    Owner->handleClick(event);
}
} // namespace panels::settings
