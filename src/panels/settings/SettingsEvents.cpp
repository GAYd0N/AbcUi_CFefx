// Extracted from SettingsPage.cpp (UTF-8). Responsibility: SettingsEvents.
#include "panels/settings/SettingsUi.h"
#include "panels/settings/SettingsPage.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include "services/settings/SettingsModel.h"
#include "services/settings/KeyBindModel.h"
#include "services/settings/VideoModeModel.h"
#include "services/settings/VoiceModel.h"
#include <cstdlib>
#include <string>
namespace panels::settings {
using namespace detail;
void SettingsPage::handleClick(Rml::Event &event) {
  Rml::Element *entry = actionNode(event.GetTargetElement());
  if (!entry)
    return;
  const Rml::String action =
      entry->GetAttribute<Rml::String>("data-action", "");
  const Rml::String rowKey = entry->GetAttribute<Rml::String>("data-row", "");
  const std::string which = action;

  if (which == "settings-page") {
    selectPage(entry->GetAttribute<Rml::String>("data-page", ""));
    return;
  }
  // Spray maker: the picker layer. Its intents are handled by the spray
  // section, which owns the browser state; the page keeps the layer frame.
  if (which == kActionSprayChoose) {
    openSprayBrowser();
    return;
  }
  if (which == kActionSprayOpen || which == kActionSprayUp ||
      which == kActionSprayRoot) {
    Spray.handleAction(which, entry, document());
    return;
  }
  if (which == kActionSprayConfirm) {
    // A confirmed pick travels to the module (decode/encode/write); the
    // layer closes when the answer lands (setSprayResult).
    Spray.handleAction(which, entry, document());
    return;
  }
  if (which == kActionSprayCancel) {
    closeSprayBrowser();
    return;
  }
  if (which == kActionApply) {
    applyAndReport();
  } else if (which == kActionReset) {
    showLayer(kLayerReset); // restore-defaults always confirms first
  } else if (which == kActionResetYes) {
    resetCurrentPage();
    hideLayers();
    render();
  } else if (which == kActionResetNo) {
    hideLayers();
  } else if (which == kActionLeaveDiscard) {
    // The discard IS the cancel: no write, drafts gone (all models),
    // the mic-test session stops, then return.
    if (Model)
      Model->cancel();
    if (BindModel)
      BindModel->cancel();
    if (VideoModel)
      VideoModel->cancelStage();
    if (VoiceModel_) {
      VoiceModel_->stopTest();
      if (VoiceModel_->available())
        VoiceModel_->snapshot(); // drafts reset to the device truth
    }
    hideLayers();
    render();
    const ActionSink action2 = Sink;
    if (action2)
      action2(kActionReturn);
  } else if (which == kActionLeaveStay) {
    hideLayers();
  } else if (which == kActionToggle && !rowKey.empty()) {
    if (Model) {
      const auto view = Model->find(rowKey.c_str());
      if (view && !view->missing) {
        // Toggle kinds: kBool threshold, kNegate sign view,
        // kCommandToggle target state - all through setDraftBool.
        const bool current =
            view->state == services::settings::RowState::kInvalid
                ? (view->row->Kind == services::settings::SettingKind::kNegate
                       ? view->engineValue < 0.0f
                       : isOn(view->engineValue))
                : toggleDraftOn(*view);
        Model->setDraftBool(rowKey.c_str(), !current);
      }
    }
    render();
  } else if (which == kActionVoiceTest) {
    // Start/stop the microphone test. The engine refusing the start
    // (no device) leaves the state untouched - the status keeps the
    // idle text; the level readout only appears on a real session.
    if (VoiceModel_ && VoiceModel_->available()) {
      if (VoiceModel_->testing())
        VoiceModel_->stopTest();
      else
        VoiceModel_->startTest();
    }
    renderVoiceSection();
  } else if (which == kActionVoiceBoost) {
    if (VoiceModel_ && VoiceModel_->available())
      VoiceModel_->setMicBoostDraft(!(VoiceModel_->micBoostDirty()
                                          ? VoiceModel_->micBoostDraft()
                                          : VoiceModel_->micBoost()));
    renderVoiceSection();
  } else if (which == kActionAdopt && !rowKey.empty()) {
    if (Model)
      Model->adoptEngineValue(rowKey.c_str());
    render();
  } else if (which == kActionBindCapture && !rowKey.empty()) {
    // Start a capture session: the model takes the row, the module takes
    // the router token (kCapture tier) and the InputBridge clamp. A
    // click on an already-capturing row is a no-op (the model refuses a
    // second session).
    if (BindModel && BindModel->beginCapture(std::atoi(rowKey.c_str()))) {
      const ActionSink action2 = Sink;
      if (action2)
        action2(kActionBindCaptureBegin);
    }
    render();
  } else if (which == kActionBindClear && !rowKey.empty()) {
    if (BindModel)
      BindModel->clearActionDraft(std::atoi(rowKey.c_str()));
    render();
  } else if (which == kActionBindRemove && !rowKey.empty()) {
    // data-row carries "<action>:<keyname>" for the single-key remove.
    const auto sep = rowKey.find(':');
    if (BindModel && sep != Rml::String::npos) {
      BindModel->removeKeyFromAction(std::atoi(rowKey.substr(0, sep).c_str()),
                                     rowKey.substr(sep + 1).c_str());
    }
    render();
  } else if (which == kActionBindReset) {
    // Bind defaults always confirm first; without a bind model the
    // whole section is absent, so the intent is a no-op (fail-safe).
    if (BindModel)
      showLayer(kLayerBindReset);
  } else if (which == kActionBindResetYes) {
    if (BindModel)
      BindModel->resetDefaults();
    hideLayers();
    render();
  } else if (which == kActionBindResetNo) {
    hideLayers();
  } else if (which == kActionBindConflictYes) {
    if (BindModel)
      BindModel->resolveConflict(true);
    render(); // the layer hides itself once the conflict is cleared
  } else if (which == kActionBindConflictNo) {
    if (BindModel)
      BindModel->resolveConflict(false);
    render();
  } else if (which == kActionVideoApply) {
    // The restart-driving apply belongs to the module (it also presents
    // the confirmation dialog afterwards): report the fixed intent.
    const ActionSink action2 = Sink;
    if (action2)
      action2(kActionVideoApply);
    render();
  } else if (which == kActionVideoWindowed) {
    if (VideoModel && VideoModel->available() &&
        !VideoModel->awaitingConfirm()) {
      const services::settings::VideoMode target =
          VideoModel->hasStaged() ? VideoModel->stagedTarget()
                                  : VideoModel->actual();
      VideoModel->stageWindowed(!target.Windowed);
    }
    render();
  } else if (which == kActionReturn) {
    requestLeave(); // clean: reports through the sink; dirty: prompts
  }
  // Unknown actions are dropped: the page only binds its own names.
}

void SettingsPage::handleChange(Rml::Event &event) {
  // Re-entrancy guard: the selects dispatch change while render() builds
  // their options (the T12 Add() pin) - programmatic churn from the
  // projection itself is NOT user input and commits nothing.
  if (InRender_)
    return;
  Rml::Element *target = event.GetTargetElement();
  if (!target)
    return;
  // T12 resolution select: the value is "WxH" from the enumerated option
  // set; the model validates it again before staging.
  if (target->GetId() == "video-resolution") {
    if (VideoModel && VideoModel->available() &&
        !VideoModel->awaitingConfirm()) {
      const Rml::String text =
          event.GetParameter<Rml::String>("value", Rml::String());
      const auto sep = text.find('x');
      if (sep != Rml::String::npos) {
        const int width = std::atoi(text.substr(0, sep).c_str());
        const int height = std::atoi(text.substr(sep + 1).c_str());
        // Re-entrancy guard: a programmatic SetAttribute dispatch can
        // re-enter here with the value already staged.
        const services::settings::VideoMode &current =
            VideoModel->hasStaged() ? VideoModel->stagedTarget()
                                    : VideoModel->actual();
        if (current.Width != width || current.Height != height)
          VideoModel->stageResolution(width, height);
      }
    }
    if (!InRender_)
      render();
    return;
  }
  if (target->GetId() == "voice-mic-volume") {
    // Mic volume commits through the display scale (0..100 -> 0..1);
    // the model clamps and marks the draft dirty, the device write
    // waits for Apply like every other row.
    if (VoiceModel_ && VoiceModel_->available()) {
      const Rml::String text =
          event.GetParameter<Rml::String>("value", Rml::String());
      float value = 0.0f;
      if (parseNumber(text.c_str(), value))
        VoiceModel_->setMicVolumeDraft(value / 100.0f);
    }
    VoiceVolumeSkipField = target;
    if (!InRender_)
      renderVoiceSection();
    VoiceVolumeSkipField = nullptr;
    return;
  }
  if (!Model)
    return;
  Rml::Element *entry = target;
  while (entry && !entry->HasAttribute("data-row"))
    entry = entry->GetParentNode();
  if (!entry)
    return;
  const Rml::String rowKey = entry->GetAttribute<Rml::String>("data-row", "");
  if (rowKey.empty())
    return;
  const Rml::String text =
      event.GetParameter<Rml::String>("value", Rml::String());
  // Select rows (kEnum/kStringEnum) commit through their value string and
  // text rows (kText) through the validated string door; the model
  // re-validates against the option set / charset either way.
  const auto view = Model->find(rowKey.c_str());
  if (view && view->row->Kind == services::settings::SettingKind::kStringEnum) {
    Model->setDraftString(rowKey.c_str(), text.c_str());
  } else if (view &&
             view->row->Kind == services::settings::SettingKind::kText) {
    Model->setDraftString(rowKey.c_str(), text.c_str());
  } else if (view &&
             view->row->Kind == services::settings::SettingKind::kEnum) {
    float value = 0.0f;
    if (parseNumber(text.c_str(), value))
      Model->setDraftEnum(rowKey.c_str(), value);
  } else {
    Model->setDraftText(rowKey.c_str(), text.c_str());
  }
  if (!InRender_)
    render(rowKey.c_str());
}
} // namespace panels::settings
