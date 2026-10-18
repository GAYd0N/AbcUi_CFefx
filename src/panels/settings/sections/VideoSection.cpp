// Extracted video section projection from SettingsPage.cpp (UTF-8).
#include "panels/settings/SettingsUi.h"
#include "VideoSection.h"
#include "services/settings/VideoModeModel.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include "framework/TextUtil.h"
#include <abc/Format.h>
#include <string>
#include <vector>
#include <utility>
namespace panels::settings::sections {
using namespace detail;
void VideoSection::render(Rml::ElementDocument *doc,
                          services::settings::VideoModeModel *Model) {
  if (!doc)
    return;
  Rml::Element *section = doc->GetElementById("video-section");
  if (!section)
    return;
  section->SetClass("video-unavailable", !Model || !Model->available());

  Rml::Element *select = doc->GetElementById("video-resolution");
  Rml::Element *toggle = doc->GetElementById("video-windowed");
  Rml::Element *apply = doc->GetElementById("video-apply");
  Rml::Element *status = doc->GetElementById("video-status");
  // RmlUi pin: <button> instantiates as a generic Element (no form-control
  // disabled semantics - only <input>/<select> are ElementFormControl), so
  // buttons gate visually through the class while the click handlers above
  // keep their own model-state guards. The select is a real form control.
  if (!Model || !Model->available()) {
    if (status)
      status->SetInnerRML(framework::TextUtil::escapeRml(
          translated("#Settings_VideoUnavailable")));
    if (select)
      if (Rml::ElementFormControl *form =
              rmlui_dynamic_cast<Rml::ElementFormControl *>(select))
        form->SetDisabled(true);
    if (toggle)
      toggle->SetClass("video-disabled", true);
    if (apply) {
      if (Rml::ElementFormControl *form =
              rmlui_dynamic_cast<Rml::ElementFormControl *>(apply))
        form->SetDisabled(true);
      apply->SetClass("video-disabled", true);
    }
    return;
  }
  // The section may have been left locked by an earlier projection: clear
  // the lock before projecting the current state.
  if (select)
    if (Rml::ElementFormControl *form =
            rmlui_dynamic_cast<Rml::ElementFormControl *>(select))
      form->SetDisabled(false);

  // The resolution options are built ONLY once the actual mode is known
  // (a projection before the first snapshot would pollute the list with a
  // 0x0 entry - live-caught) and rebuilt only when the set changes. The
  // control's own Add() is used, not SetInnerRML: Add routes through the
  // widget so the option is matchable in the SAME pass, while raw RML
  // children only transfer on the next Context::Update (live-caught:
  // SetAttribute("value") before the transfer leaves the raw value as the
  // displayed text). (RmlUi pin: ElementFormControlSelect::Add ->
  // WidgetDropDown::AddOption.)
  auto *selectControl =
      select ? rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(select)
             : nullptr;
  if (selectControl && Model->actualKnown()) {
    std::vector<std::pair<int, int>> options;
    options.emplace_back(Model->actual().Width, Model->actual().Height);
    for (const auto &mode : Model->modes()) {
      bool known = false;
      for (const auto &existing : options)
        known = known || (existing.first == mode.Width &&
                          existing.second == mode.Height);
      if (!known)
        options.emplace_back(mode.Width, mode.Height);
    }
    if (options != OptionsShown) {
      selectControl->RemoveAll();
      for (const auto &option : options) {
        const std::string value =
            std::to_string(option.first) + "x" + std::to_string(option.second);
        selectControl->Add(framework::TextUtil::escapeRml(value), value);
      }
      OptionsShown = options;
    }
  }

  // Projection values: the staged target over the actual mode. Selection
  // goes through SetSelection(index): it reconciles the widget without
  // dispatching a change event, so no feedback loop is possible. (A raw
  // SetAttribute("value") DOES dispatch change - the stack-overflow trap
  // the offline test caught; kept only as the no-widget fallback.)
  const bool stagedResolution = Model->hasStaged();
  services::settings::VideoMode target =
      stagedResolution ? Model->stagedTarget() : Model->actual();
  if (selectControl && !OptionsShown.empty()) {
    int index = -1;
    for (size_t i = 0; i < OptionsShown.size(); ++i) {
      if (OptionsShown[i].first == target.Width &&
          OptionsShown[i].second == target.Height) {
        index = static_cast<int>(i);
        break;
      }
    }
    if (index >= 0 && selectControl->GetSelection() != index)
      selectControl->SetSelection(index);
  } else if (select) {
    const std::string value =
        std::to_string(target.Width) + "x" + std::to_string(target.Height);
    if (select->GetAttribute<Rml::String>("value", "") != value)
      select->SetAttribute("value", value);
  }
  if (toggle) {
    toggle->SetInnerRML(framework::TextUtil::escapeRml(
        translated(target.Windowed ? "#Settings_VideoWindowedOn"
                                   : "#Settings_VideoWindowedOff")));
  }
  // While a confirmation is outstanding the whole section is locked (the
  // pending switch owns the engine state); otherwise the controls follow
  // the stage.
  const bool locked = Model->awaitingConfirm();
  const bool canApply = Model->hasStaged() && !locked;
  if (select)
    if (Rml::ElementFormControl *form =
            rmlui_dynamic_cast<Rml::ElementFormControl *>(select))
      form->SetDisabled(locked);
  if (toggle)
    toggle->SetClass("video-disabled", locked);
  if (apply) {
    if (Rml::ElementFormControl *form =
            rmlui_dynamic_cast<Rml::ElementFormControl *>(apply))
      form->SetDisabled(!canApply);
    apply->SetClass("video-disabled", !canApply);
  }

  if (status) {
    std::string text = abc::Format(
        "{0} {1}", translated("#Settings_VideoActual"),
        services::settings::VideoModeModel::describe(Model->actual()));
    text += " | ";
    if (Model->awaitingConfirm()) {
      text += translated("#Settings_VideoAwaiting");
    } else if (Model->hasStaged()) {
      text += abc::Format("{0} {1}", translated("#Settings_VideoStaged"),
                          services::settings::VideoModeModel::describe(target));
    } else {
      text += translated("#Settings_VideoNone");
    }
    status->SetInnerRML(framework::TextUtil::escapeRml(text));
  }
}
} // namespace panels::settings::sections
