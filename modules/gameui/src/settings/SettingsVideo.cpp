// The display-mode card and the display-mode confirmation dialog: the
// projection of the video facts, the windowed toggle, and the page-owned
// confirmation document (Keep/Revert) presented from the same paint beat.

#include "../shared/PageShared.h"
#include "SettingsInternal.h"

#include <cstring>
#include <string>
#include <utility>
#include <string_view>

namespace settingspage {

// ------------------------------------------------------- display modes ----

std::string describeMode(int32_t width, int32_t height, int32_t windowed) {
  char buffer[ABCUI_SETTINGS_TEXT_MAX];
  buffer[0] = '\0';
  if (!Api->settings || !Api->settings->videoDescribe(width, height, windowed,
                                                      buffer, sizeof(buffer)))
    return {};
  return buffer;
}

void renderVideoSection() {
  if (!Page)
    return;
  AbcUiElement *section = node("video-section");
  if (!section)
    return;
  AbcUiVideoFacts facts{};
  const bool available = videoFacts(facts) && facts.available;
  Api->ui->elementSetClass(section, "video-unavailable", !available);

  AbcUiElement *select = node("video-resolution");
  AbcUiElement *toggle = node("video-windowed");
  AbcUiElement *apply = node("video-apply");
  // RmlUi pin: <button> instantiates as a generic Element (no form-control
  // disabled semantics - only <input>/<select> are form controls), so buttons
  // gate visually through the class while the click handlers keep their own
  // model-state guards. The select is a real form control.
  if (!available) {
    setInnerRml(node("video-status"),
                escapeText(resolve("#abcui.gameui/Settings_VideoUnavailable")));
    Api->ui->elementSetDisabled(select, 1);
    Api->ui->elementSetClass(toggle, "video-disabled", 1);
    Api->ui->elementSetDisabled(apply, 1);
    Api->ui->elementSetClass(apply, "video-disabled", 1);
    return;
  }
  // The section may have been left locked by an earlier projection: clear
  // the lock before projecting the current state.
  Api->ui->elementSetDisabled(select, 0);

  // The resolution options are built ONLY once the actual mode is known (a
  // projection before the first snapshot would pollute the list with a 0x0
  // entry) and rebuilt only when the set changes. The control's own Add() is
  // used, not inner RML: Add routes through the widget so the option is
  // matchable in the SAME pass.
  std::vector<std::pair<int, int>> options;
  if (facts.actualKnown) {
    options.emplace_back(facts.actualWidth, facts.actualHeight);
    for (int32_t index = 0; index < facts.modeCount; ++index) {
      int32_t width = 0;
      int32_t height = 0;
      if (!Api->settings->videoModeAt(index, &width, &height))
        continue;
      bool known = false;
      for (const auto &existing : options)
        known = known || (existing.first == width && existing.second == height);
      if (!known)
        options.emplace_back(width, height);
    }
  }
  if (select && options != VideoOptionsShown) {
    Api->ui->elementSelectRemoveAll(select);
    for (const auto &option : options) {
      const std::string value =
          std::to_string(option.first) + "x" + std::to_string(option.second);
      const std::string label = escapeText(value);
      Api->ui->elementSelectAdd(select, label.c_str(), value.c_str());
    }
    VideoOptionsShown = options;
  }

  // Projection values: the staged target over the actual mode. Selection goes
  // through SetSelection: it reconciles the widget without dispatching a
  // change event, so no feedback loop is possible.
  const bool stagedResolution = facts.hasStaged != 0;
  const int32_t targetWidth =
      stagedResolution ? facts.stagedWidth : facts.actualWidth;
  const int32_t targetHeight =
      stagedResolution ? facts.stagedHeight : facts.actualHeight;
  const int32_t targetWindowed =
      stagedResolution ? facts.stagedWindowed : facts.actualWindowed;
  if (select && !VideoOptionsShown.empty()) {
    int32_t selection = -1;
    for (std::size_t index = 0; index < VideoOptionsShown.size(); ++index) {
      if (VideoOptionsShown[index].first == targetWidth &&
          VideoOptionsShown[index].second == targetHeight) {
        selection = static_cast<int32_t>(index);
        break;
      }
    }
    if (selection >= 0 && Api->ui->elementSelectSelection(select) != selection)
      Api->ui->elementSelectSetSelection(select, selection);
  }
  if (toggle) {
    setInnerRml(toggle, escapeText(resolve(
                            targetWindowed
                                ? "#abcui.gameui/Settings_VideoWindowedOn"
                                : "#abcui.gameui/Settings_VideoWindowedOff")));
  }
  // While a confirmation is outstanding the whole section is locked (the
  // pending switch owns the engine state); otherwise the controls follow the
  // stage.
  const bool locked = facts.awaitingConfirm != 0;
  const bool canApply = facts.hasStaged != 0 && !locked;
  Api->ui->elementSetDisabled(select, locked);
  Api->ui->elementSetClass(toggle, "video-disabled", locked);
  Api->ui->elementSetDisabled(apply, !canApply);
  Api->ui->elementSetClass(apply, "video-disabled", !canApply);

  std::string text =
      resolve("#abcui.gameui/Settings_VideoActual") + " " +
      describeMode(facts.actualWidth, facts.actualHeight, facts.actualWindowed);
  text += " | ";
  if (locked) {
    text += resolve("#abcui.gameui/Settings_VideoAwaiting");
  } else if (facts.hasStaged) {
    text += resolve("#abcui.gameui/Settings_VideoStaged") + " " +
            describeMode(targetWidth, targetHeight, targetWindowed);
  } else {
    text += resolve("#abcui.gameui/Settings_VideoNone");
  }
  setInnerRml(node("video-status"), escapeText(text));
}

void renderConfirm(bool focusDefault) {
  if (!Confirm)
    return;
  AbcUiVideoFacts facts{};
  if (!videoFacts(facts))
    return;
  // The headline states the mode the engine ACTUALLY runs, plus what the
  // engine was asked to switch to (the request alone proves nothing).
  const std::string actualText =
      facts.actualKnown ? describeMode(facts.actualWidth, facts.actualHeight,
                                       facts.actualWindowed)
                        : resolve("#abcui.gameui/VideoConfirm_Unknown");
  setInnerRml(confirmNode("videoconfirm-actual"), escapeText(actualText));
  setInnerRml(
      confirmNode("videoconfirm-requested"),
      escapeText(describeMode(facts.requestedWidth, facts.requestedHeight,
                              facts.requestedWindowed)));
  const char *lead = "#abcui.gameui/VideoConfirm_Lead";
  switch (facts.confirmKind) {
  case ABCUI_VIDEO_CONFIRM_APPLIED:
    lead = "#abcui.gameui/VideoConfirm_Lead";
    break;
  case ABCUI_VIDEO_CONFIRM_NOT_APPLIED:
    lead = "#abcui.gameui/VideoConfirm_NotApplied";
    break;
  default:
    lead = "#abcui.gameui/VideoConfirm_Unknown";
    break;
  }
  setInnerRml(confirmNode("videoconfirm-lead"), escapeText(resolve(lead)));
  std::string timeout = resolve("#abcui.gameui/VideoConfirm_TimeoutTime");
  const std::string marker = "{0}";
  const std::string seconds = std::to_string(facts.remainingSeconds);
  const std::size_t at = timeout.find(marker);
  if (at != std::string::npos)
    timeout.replace(at, marker.size(), seconds);
  else
    timeout += " " + seconds;
  setInnerRml(confirmNode("videoconfirm-timeout"), escapeText(timeout));

  // The Keep entry is only offered when the switch actually took effect; the
  // model refuses anything else anyway (fail-closed projection).
  AbcUiElement *keep = confirmNode("videoconfirm-keep");
  const bool canKeep = facts.confirmKind == ABCUI_VIDEO_CONFIRM_APPLIED;
  Api->ui->elementSetClass(keep, "video-disabled", !canKeep);
  Api->ui->elementSetDisabled(keep, !canKeep);
  if (focusDefault) {
    if (AbcUiElement *focus = confirmNode("videoconfirm-revert"))
      Api->ui->elementFocus(focus);
  }
}

bool projectVideoConfirm() {
  if (!Confirm)
    return false;
  AbcUiVideoFacts facts{};
  const bool awaiting = videoFacts(facts) && facts.awaitingConfirm;
  const bool visible = Api->documents->visible(Confirm) != 0;
  if (awaiting) {
    // The projection is change-gated on the page generation: the facts
    // re-query the engine's actual mode, so a per-frame rewrite would be
    // wasted work while the countdown ticks.
    const bool fresh = !visible;
    if (fresh)
      Api->documents->show(Confirm);
    const uint32_t revision = Api->settings ? Api->settings->revision() : 0;
    if (fresh || revision != ConfirmRenderedRevision) {
      ConfirmRenderedRevision = revision;
      renderConfirm(fresh);
    }
  } else if (visible) {
    Api->documents->hide(Confirm);
    ConfirmRenderedRevision = ~0u;
  }
  return awaiting;
}

void onConfirmClick(AbcUiElement *target) {
  if (!target || !Api->settings)
    return;
  AbcUiElement *entry = actionNode(target);
  if (!entry)
    return;
  const std::string action = attributeOf(entry, "data-action");
  if (action == "video-keep") {
    Api->settings->videoKeep();
  } else if (action == "video-revert") {
    Api->settings->videoRevert();
  }
}

int32_t onConfirmClickEvent(void *, AbcUiElement *target,
                            const AbcUiElementEvent *event) {
  if (!event || !target || std::string_view(event->type) != "click")
    return 0;
  onConfirmClick(target);
  return 0;
}

void onConfirmReady(void *) {
  if (!Confirm)
    return;
  ConfirmRoot = Api->documents->root(Confirm);
  if (ConfirmRoot)
    ConfirmClickBinding = Api->ui->elementAddListener(
        ConfirmRoot, "click", &onConfirmClickEvent, nullptr);
}

void onConfirmClose(void *) {
  if (ConfirmRoot && ConfirmClickBinding)
    Api->ui->elementRemoveListener(ConfirmRoot, "click", ConfirmClickBinding);
  ConfirmClickBinding = 0;
  ConfirmRoot = nullptr;
}

} // namespace settingspage
