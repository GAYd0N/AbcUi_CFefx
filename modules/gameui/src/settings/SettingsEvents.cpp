// The page's delegated DOM events: click, change, dblclick and mousedown,
// dispatched to the section handlers.

#include "../shared/PageShared.h"
#include "SettingsInternal.h"

#include <cstring>
#include <cstdlib>
#include <string>
#include <string_view>

namespace settingspage {

bool rowEditValue(AbcUiElement *target, std::string &value) {
  char buffer[ABCUI_SETTINGS_TEXT_MAX * 4];
  buffer[0] = '\0';
  if (!target || Api->ui->elementValue(target, buffer, sizeof(buffer)) <= 0)
    return false;
  value = buffer;
  return true;
}

void onChange(AbcUiElement *target) {
  // Re-entrancy guard: the selects dispatch change while render() builds
  // their options - programmatic churn from the projection itself is NOT user
  // input and commits nothing.
  if (InRender || !target || !Api->settings)
    return;
  const std::string id = Api->ui->elementId(target);
  // RGBA colour pickers: one change carries four channel values, so the
  // row-keyed commit below cannot express it. The picker is not a form
  // control: its canonical value rides the element's own value attribute
  // (written before the change is dispatched).
  if (bindingFor(id.c_str())) {
    handleColorChange(id.c_str(), attributeOf(target, "value").c_str());
    render();
    return;
  }
  std::string value;
  if (!rowEditValue(target, value))
    return;
  // T12 resolution select: the value is "WxH" from the enumerated option set;
  // the model validates it again before staging.
  if (id == "video-resolution") {
    AbcUiVideoFacts facts{};
    if (videoFacts(facts) && facts.available && !facts.awaitingConfirm) {
      const std::size_t separator = value.find('x');
      if (separator != std::string::npos) {
        const auto width =
            parseInt(std::string_view(value).substr(0, separator));
        const auto height =
            parseInt(std::string_view(value).substr(separator + 1));
        // Re-entrancy guard: a programmatic value write can re-enter here
        // with the value already staged.
        const int currentWidth =
            facts.hasStaged ? facts.stagedWidth : facts.actualWidth;
        const int currentHeight =
            facts.hasStaged ? facts.stagedHeight : facts.actualHeight;
        if (width && height &&
            (currentWidth != *width || currentHeight != *height))
          Api->settings->videoStageResolution(*width, *height);
      }
    }
    render();
    return;
  }
  if (id == "model-picker-filter") {
    // The filter commits on the control's change (Enter/focus loss, like
    // every text row); the picker re-projects its own list only.
    handleModelFilterChange(value);
    return;
  }
  if (id == "voice-mic-volume") {
    // Mic volume commits through the display scale (0..100 -> 0..1); the
    // model clamps and marks the draft dirty, the device write waits for
    // Apply like every other row.
    AbcUiVoiceFacts facts{};
    float parsed = 0.0f;
    if (voiceFacts(facts) && facts.available &&
        parseNumber(value.c_str(), parsed))
      Api->settings->voiceSetVolumeDraft(parsed / 100.0f);
    VoiceVolumeSkip = target;
    if (!InRender)
      renderVoiceSection();
    VoiceVolumeSkip = nullptr;
    return;
  }
  // Row commit: the data-row attribute lives on the control or an ancestor.
  AbcUiElement *entry = target;
  while (entry && attributeOf(entry, "data-row").empty())
    entry = Api->ui->elementParent(entry);
  if (!entry)
    return;
  const std::string rowKey = attributeOf(entry, "data-row");
  if (rowKey.empty())
    return;
  AbcUiSettingRow row{};
  if (!rowFound(rowKey.c_str(), row))
    return;
  // Select initialization can deliver a deferred change after projection.
  // An unchanged value is not a new edit and must not schedule another pass.
  if (value == row.draftText)
    return;
  const bool projectionCurrent = Api->settings->revision() == RenderedRevision;
  // Select rows (kEnum/kStringEnum) commit through their value string and
  // text rows (kText) through the validated string door; the model
  // re-validates against the option set / charset either way.
  if (row.kind == ABCUI_SET_KIND_STRING_ENUM ||
      row.kind == ABCUI_SET_KIND_TEXT) {
    Api->settings->setDraftString(rowKey.c_str(), value.c_str());
  } else if (row.kind == ABCUI_SET_KIND_ENUM) {
    float parsed = 0.0f;
    if (parseNumber(value.c_str(), parsed))
      Api->settings->setDraftEnum(rowKey.c_str(), parsed);
  } else {
    Api->settings->setDraftText(rowKey.c_str(), value.c_str());
  }
  if (projectionCurrent)
    renderEditedRow(rowKey.c_str(), true);
  else
    render(rowKey.c_str());
}

void onClick(AbcUiElement *target) {
  if (!target || !Api->settings)
    return;
  AbcUiElement *entry = actionNode(target);
  if (!entry)
    return;
  const std::string action = attributeOf(entry, "data-action");
  const std::string rowKey = attributeOf(entry, "data-row");
  if (action.empty())
    return;

  // The model picker's intents come first - the list rows are generic
  // data-action nodes the chain below would drop. The context menu item acts
  // and closes the menu itself.
  if (handleModelContextAction(action)) {
    if (!InRender)
      render();
    return;
  }
  if (handleModelAction(action, entry)) {
    if (!InRender)
      render();
    return;
  }
  if (action == "settings-page") {
    selectPage(attributeOf(entry, "data-page"));
    return;
  }
  // Spray maker: the picker layer. Its intents are handled by the picker
  // state; the page keeps the layer frame.
  if (action == kActionSprayChoose) {
    openSprayBrowser();
    return;
  }
  if (action == kActionSprayOpen || action == kActionSprayUp ||
      action == kActionSprayRoot || action == kActionSprayConfirm) {
    handleSprayAction(action, entry);
    return;
  }
  if (action == kActionSprayCancel) {
    closeSprayBrowser();
    refocusDefault();
    return;
  }
  if (action == kActionApply) {
    applyAndReport();
  } else if (action == kActionReset) {
    showLayer(kLayerReset); // restore-defaults always confirms first
  } else if (action == kActionResetYes) {
    resetCurrentPage();
    hideLayers();
    render();
    refocusDefault();
  } else if (action == kActionResetNo) {
    hideLayers();
    refocusDefault();
  } else if (action == kActionLeaveDiscard) {
    discardAndReturn();
  } else if (action == kActionLeaveStay) {
    hideLayers();
    refocusDefault();
  } else if (action == kActionToggle && !rowKey.empty()) {
    const bool projectionCurrent =
        Api->settings->revision() == RenderedRevision;
    AbcUiSettingRow row{};
    if (rowFound(rowKey.c_str(), row) && !(row.flags & ABCUI_SET_ROW_MISSING)) {
      // Toggle kinds: kBool threshold, kNegate sign view, kCommandToggle
      // target state - all through setDraftBool.
      const bool current =
          row.state == ABCUI_SET_STATE_INVALID
              ? (row.kind == ABCUI_SET_KIND_NEGATE ? row.engineValue < 0.0f
                                                   : isOn(row.engineValue))
              : toggleDraftOn(row);
      Api->settings->setDraftBool(rowKey.c_str(), current ? 0 : 1);
    }
    if (projectionCurrent)
      renderEditedRow(rowKey.c_str(), false);
    else
      render();
  } else if (action == kActionVoiceTest) {
    // Start/stop the microphone test. The engine refusing the start (no
    // device) leaves the state untouched - the status keeps the idle text.
    AbcUiVoiceFacts facts{};
    if (voiceFacts(facts) && facts.available) {
      if (facts.testing)
        Api->settings->voiceStopTest();
      else
        Api->settings->voiceStartTest();
    }
    renderVoiceSection();
  } else if (action == kActionVoiceBoost) {
    AbcUiVoiceFacts facts{};
    if (voiceFacts(facts) && facts.available) {
      const bool on =
          facts.micBoostDirty ? facts.micBoostDraft != 0 : facts.micBoost != 0;
      Api->settings->voiceSetBoostDraft(on ? 0 : 1);
    }
    renderVoiceSection();
  } else if (action == kActionAdopt && !rowKey.empty()) {
    Api->settings->adopt(rowKey.c_str());
    render();
  } else if (action == kActionBindCapture && !rowKey.empty()) {
    // Start a capture session: the model takes the row, the framework takes
    // the router token (kCapture tier) and the input clamp. A click on an
    // already-capturing row is a no-op (the model refuses a second session).
    Api->settings->bindBeginCapture(parseInt(rowKey).value_or(0));
    render();
  } else if (action == kActionBindClear && !rowKey.empty()) {
    Api->settings->bindClear(parseInt(rowKey).value_or(0));
    render();
  } else if (action == kActionBindRemove && !rowKey.empty()) {
    // data-row carries "<action>:<keyname>" for the single-key remove.
    const std::size_t separator = rowKey.find(':');
    if (separator != std::string::npos) {
      Api->settings->bindRemove(
          parseInt(std::string_view(rowKey).substr(0, separator)).value_or(0),
          rowKey.substr(separator + 1).c_str());
    }
    render();
  } else if (action == kActionBindReset) {
    // Bind defaults always confirm first; without a bind model the whole
    // section is absent, so the intent is a no-op (fail-safe).
    if (Api->settings->bindReadsAvailable())
      showLayer(kLayerBindReset);
  } else if (action == kActionBindResetYes) {
    Api->settings->bindResetDefaults();
    hideLayers();
    render();
    refocusDefault();
  } else if (action == kActionBindResetNo) {
    hideLayers();
    refocusDefault();
  } else if (action == kActionBindConflictYes) {
    Api->settings->bindResolveConflict(1);
    render(); // the layer hides itself once the conflict is cleared
    refocusDefault();
  } else if (action == kActionBindConflictNo) {
    Api->settings->bindResolveConflict(0);
    render();
    refocusDefault();
  } else if (action == kActionVideoApply) {
    // The restart-driving apply belongs to the framework transaction, which
    // also presents the confirmation afterwards.
    Api->settings->videoApply();
    render();
  } else if (action == kActionVideoWindowed) {
    AbcUiVideoFacts facts{};
    if (videoFacts(facts) && facts.available && !facts.awaitingConfirm) {
      const bool windowed = facts.hasStaged ? facts.stagedWindowed != 0
                                            : facts.actualWindowed != 0;
      Api->settings->videoStageWindowed(windowed ? 0 : 1);
    }
    render();
  } else if (action == kActionReturn) {
    if (!requestLeave())
      Api->menu->back(); // clean: the menu owns the navigation
  }
  // Unknown actions are dropped: the page only binds its own names.
}

int32_t onRootClick(void *, AbcUiElement *target,
                    const AbcUiElementEvent *event) {
  if (!event || !target || std::string_view(event->type) != "click")
    return 0;
  onClick(target);
  return 0;
}

int32_t onRootChange(void *, AbcUiElement *target,
                     const AbcUiElementEvent *event) {
  if (!event || !target || std::string_view(event->type) != "change")
    return 0;
  onChange(target);
  return 0;
}

int32_t onRootDblClick(void *, AbcUiElement *target,
                       const AbcUiElementEvent *event) {
  if (!event || !target || std::string_view(event->type) != "dblclick")
    return 0;
  handleModelDblClick(target);
  return 0;
}

int32_t onRootMouseDown(void *, AbcUiElement *target,
                        const AbcUiElementEvent *event) {
  if (!event || !target || std::string_view(event->type) != "mousedown")
    return 0;
  if (event->button == 1)
    handleModelPointerDown(target, 1, event->mouseX, event->mouseY);
  else if (event->button == 0)
    handleModelPointerDown(target, 0, event->mouseX, event->mouseY);
  return 0;
}

} // namespace settingspage
