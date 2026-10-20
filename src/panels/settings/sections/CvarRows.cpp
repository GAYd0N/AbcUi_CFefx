// Extracted from SettingsPage.cpp (UTF-8). Responsibility: sections/CvarRows.
#include "panels/settings/SettingsUi.h"
#include "panels/settings/SettingsPage.h"
#include "panels/settings/sections/ColorRows.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include "services/settings/SettingsModel.h"
#include "framework/ColorUtil.h"
#include "framework/TextUtil.h"
#include <abc/Format.h>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
namespace panels::settings {
using namespace detail;
namespace {
// A range widget's parts are non-DOM children (WidgetSlider appends them
// after the DOM ones), so the thumb is found by tag, not by selector.
Rml::Element *sliderThumb(Rml::Element *input) {
  if (!input)
    return nullptr;
  for (int Index = input->GetNumChildren(true) - 1; Index >= 0; --Index) {
    Rml::Element *Child = input->GetChild(Index);
    if (Child && Child->GetTagName() == "sliderbar")
      return Child;
  }
  return nullptr;
}

// The dropdown label for a file-backed choice: the file name without its
// directory or extension (the value keeps the full relative path).
std::string fileStem(const std::string &value) {
  const size_t slash = value.find_last_of("/\\");
  const size_t begin = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = value.find_last_of('.');
  const size_t end =
      dot != std::string::npos && dot > begin ? dot : value.size();
  return value.substr(begin, end - begin);
}
} // namespace

void SettingsPage::buildRows() {
  if (!Model || !document())
    return;
  Rml::Element *Rows = document()->GetElementById("rows");
  if (!Rows)
    return;
  // One skeleton block per description-table row; row DOM ids derive from
  // the cvar names ([a-z_], safe as ids). Delegated listeners need no
  // per-element bindings here. Rows are grouped into the page's declared
  // section cards; a card's widget blocks are moved in later by
  // projectWidgets() (the blocks are singletons in settings.rml).
  std::string Markup;
  const auto Views = Model->rows();
  auto AppendRow = [&](const services::settings::RowView &View) {
    Markup += "<div class=\"row\" id=\"row-";
    Markup += framework::TextUtil::escapeRmlAttribute(View.row->Cvar);
    Markup += "\"><div class=\"row-label\"></div>";
    if (isToggleKind(View.row->Kind)) {
      Markup +=
          "<button class=\"row-toggle\" data-action=\"toggle\" data-row=\"";
    } else if (isSelectKind(View.row->Kind)) {
      // Options transfer through the widget Add() at render time (the
      // inline-option transfer is a next-Update delay - T12 pin); the
      // skeleton only carries the empty select.
      Markup += "<select class=\"row-select\" data-row=\"";
    } else {
      const bool Slider =
          View.row->Kind == services::settings::SettingKind::kFloat &&
          View.row->Bounded && View.row->Min < View.row->Max;
      Markup += "<input class=\"row-input";
      // Hue rows carry the drag thumb's own colour swatch (the style
      // sheet swaps their hover/focus feedback onto the thumb border).
      if (View.row->HueSwatch)
        Markup += " row-hue";
      if (Slider)
        Markup += " row-slider\" type=\"range\" min=\"" +
                  numberText(View.row->Min) + "\" max=\"" +
                  numberText(View.row->Max) + "\" step=\"" +
                  numberText(View.row->Step > 0
                                 ? View.row->Step
                                 : (View.row->Max - View.row->Min) / 1000) +
                  "\" value=\"" + numberText(View.draftValue);
      else
        Markup += "\" type=\"text";
      Markup += "\" data-row=\"";
    }
    Markup += framework::TextUtil::escapeRmlAttribute(View.row->Cvar);
    Markup += "\"/><span class=\"row-value\"></span><div "
              "class=\"row-status\"></div></div>";
  };
  auto OpenCard = [&](const std::string &CardId, const std::string &TitleToken,
                      bool HasPreview, bool HasBlock, bool Stacked,
                      bool TwoUp) {
    Markup += "<div class=\"settings-card";
    if (HasPreview)
      Markup += " has-preview";
    // A full-width block above the rows only exists on a card without a
    // preview column; on a preview card the block shares that column and
    // stacks under the preview.
    if (HasBlock && !HasPreview)
      Markup += " has-block";
    if (Stacked)
      Markup += " has-side-stack";
    if (TwoUp)
      Markup += " layout-two-up";
    Markup += "\" id=\"" + CardId + "\">";
    if (!TitleToken.empty())
      Markup += "<div class=\"settings-card-title section-header\">" +
                framework::TextUtil::escapeRml(translated(TitleToken.c_str())) +
                "</div>";
    if (HasPreview) {
      // Preview card: the rows and the widget column share one flex row
      // (the rows keep their left column, the previews take the rest of
      // the card), so a live preview grows with the card instead of
      // sitting in a fixed 150dp slot with dead space beside it. The
      // widget column closes the body in CloseCard.
      Markup += "<div class=\"settings-card-body\"><div "
                "class=\"settings-card-items\" id=\"" +
                CardId + "-items\">";
    } else {
      Markup += "<div class=\"settings-card-widgets\" id=\"" + CardId +
                "-widgets\"></div>";
      Markup +=
          "<div class=\"settings-card-items\" id=\"" + CardId + "-items\">";
    }
  };
  auto CloseCard = [&](const std::string &CardId, bool HasPreview) {
    Markup += "</div>"; // items
    if (HasPreview)
      Markup += "<div class=\"settings-card-widgets\" id=\"" + CardId +
                "-widgets\"></div></div>";
    Markup += "</div>"; // card
  };
  // Configured page: one card per declared section, in declaration order.
  auto BuildDeclaredPage =
      [&](const services::uiconfig::SettingsPageDefinition *Page) {
        Markup += "<div class=\"settings-cvar-page\" id=\"settings-rows-" +
                  Page->Id + "\">";
        for (const auto &Section : Page->Sections) {
          bool HasPreview = false;
          bool HasBlock = false;
          bool HasWidget = false;
          for (const auto &Component : Section.Components)
            for (int I = 0; I < kWidgetBlockCount; ++I)
              if (Component == kWidgetBlocks[I].Component) {
                HasWidget = true;
                if (kWidgetBlocks[I].Preview)
                  HasPreview = true;
                // A row-flow block lives in the rows column, never in the
                // widget column, so it must not claim the card's block
                // layout (that class used to turn the preview column into a
                // plain block and stop the model preview from stretching).
                else if (!kWidgetBlocks[I].RowFlow)
                  HasBlock = true;
              }
          // The rows a section actually draws. Suppressed rows stay in the
          // model (drafts, apply, reset) but their widget owns the
          // presentation.
          const auto VisibleRow = [&](const auto &View) {
            if (Section.Id != View.row->Section)
              return false;
            if (std::find(Page->Cvars.begin(), Page->Cvars.end(),
                          View.row->Cvar) == Page->Cvars.end())
              return false;
            // I16: the model-picker widget owns the `model` cvar's
            // presentation in its section - the plain select row would
            // duplicate it. The row stays declared: the picker and the
            // preview ride its drafts, options and status.
            if (Section.hasComponent("model-picker") &&
                std::string(View.row->Cvar) == "model")
              return false;
            // The crosshair RGBA pickers own the eight colour rows.
            if (Section.hasComponent(sections::kColorPickerComponent) &&
                sections::ownsColorCvar(View.row->Cvar))
              return false;
            return true;
          };
          int RowCount = 0;
          for (const auto &View : Views)
            if (VisibleRow(View))
              ++RowCount;
          // A card with neither rows nor a widget is a blank band: drop it
          // from the page (the user.scr page's "more settings" target is
          // empty until a mod script appends entries there).
          if (RowCount == 0 && !HasWidget)
            continue;
          // Two-up sections put the widget band above a two-column row
          // grid; the band keeps the preview and the RGBA pickers side by
          // side, so the preview no longer owns a whole column.
          const bool TwoUp = Section.Layout == "two-up";
          const bool Stacked = !TwoUp && HasPreview && HasBlock;
          OpenCard("settings-card-" + Page->Id + "-" + Section.Id,
                   Section.Label, TwoUp ? false : HasPreview,
                   TwoUp ? false : HasBlock, Stacked, TwoUp);
          if (TwoUp) {
            int InPair = 0;
            for (const auto &View : Views) {
              if (!VisibleRow(View))
                continue;
              if (InPair == 0)
                Markup += "<div class=\"row-pair\">";
              AppendRow(View);
              if (++InPair == 2) {
                Markup += "</div>";
                InPair = 0;
              }
            }
            if (InPair != 0)
              Markup += "</div>";
          } else {
            for (const auto &View : Views)
              if (VisibleRow(View))
                AppendRow(View);
          }
          CloseCard("settings-card-" + Page->Id + "-" + Section.Id,
                    TwoUp ? false : HasPreview);
        }
        Markup += "</div>";
      };
  // Legacy projection (no page definitions, e.g. the model fixtures): items
  // group into anonymous cards by their free-form section token.
  auto BuildLegacy = [&] {
    std::string LastSection;
    bool Open = false;
    int CardIndex = 0;
    auto OpenLegacyCard = [&](const std::string &Token) {
      OpenCard("settings-card-legacy-" + std::to_string(CardIndex++), Token,
               false, false, false, false);
      Open = true;
    };
    for (const auto &View : Views) {
      if (View.row->Section && View.row->Section != LastSection) {
        LastSection = View.row->Section;
        if (Open)
          CloseCard("", false);
        OpenLegacyCard(View.row->Section);
      } else if (!Open) {
        OpenLegacyCard("");
      }
      AppendRow(View);
    }
    if (Open)
      CloseCard("", false);
  };
  if (Pages) {
    for (const auto &Page : *Pages)
      BuildDeclaredPage(&Page);
  } else {
    BuildLegacy();
  }
  Rows->SetInnerRML(Markup);
}

void SettingsPage::renderRow(const services::settings::RowView &view,
                             bool skipInput) {
  Rml::ElementDocument *doc = document();
  if (!doc)
    return;
  Rml::Element *rowNode =
      doc->GetElementById((std::string("row-") + view.row->Cvar).c_str());
  if (!rowNode)
    return;
  if (Rml::Element *label = rowNode->QuerySelector(".row-label"))
    label->SetInnerRML(
        framework::TextUtil::escapeRml(translated(view.row->LabelToken)));

  const bool toggle = isToggleKind(view.row->Kind);
  const bool select = isSelectKind(view.row->Kind);
  Rml::Element *control = rowNode->QuerySelector(
      toggle ? ".row-toggle" : (select ? ".row-select" : ".row-input"));
  if (!control)
    return;
  control->SetClass("row-missing", view.missing);
  control->SetClass("row-readonly", view.row->ReadOnly);
  const bool Editable = Model && Model->editable(view.row->Cvar);
  if (!Editable)
    control->SetAttribute("disabled", "disabled");
  else
    control->RemoveAttribute("disabled");

  if (view.missing) {
    // Unavailable: fail-closed presentation, no editable control path.
    if (Rml::ElementFormControl *form =
            rmlui_dynamic_cast<Rml::ElementFormControl *>(control))
      form->SetDisabled(true);
    if (auto *Status = rowNode->QuerySelector(".row-status"))
      Status->SetInnerRML(framework::TextUtil::escapeRml(
          view.row->RequireCvar
              ? translated("#Settings_DependencyUnavailable") + " " +
                    view.row->RequireCvar
              : translated("#Settings_Unavailable")));
    return;
  }
  if (Rml::ElementFormControl *form =
          rmlui_dynamic_cast<Rml::ElementFormControl *>(control))
    form->SetDisabled(!Editable);
  if (auto *Value = rowNode->QuerySelector(".row-value")) {
    // Only the bounded float rows carry a readout; an empty inline block
    // would wrap onto its own line inside the narrow card column and push
    // the status line away from its control.
    const bool ShowsValue =
        view.row->Kind == services::settings::SettingKind::kFloat &&
        view.row->Bounded;
    Value->SetProperty("display", ShowsValue ? "inline-block" : "none");
    Value->SetInnerRML(
        ShowsValue ? framework::TextUtil::escapeRml(view.draftText) : "");
  }
  // Hue rows (topcolor / bottomcolor): the drag thumb shows the colour the
  // value represents. The engine reads 0..255 as palette-hue degrees
  // (value * 360/255 - Renderer PaletteHueReplace), so the same mapping
  // paints the swatch; the write is skipped while the colour is unchanged
  // (a render per input event must not re-dirty the style tree).
  if (view.row->HueSwatch &&
      view.row->Kind == services::settings::SettingKind::kFloat) {
    if (Rml::Element *thumb = sliderThumb(control)) {
      float Rgb[3];
      framework::ColorUtil::playerHueColor(view.draftValue, Rgb);
      const std::string Swatch = framework::ColorUtil::format(Rgb);
      const auto Shown = HueSwatchShown.find(view.row->Cvar);
      if (Shown == HueSwatchShown.end() || Shown->second != Swatch) {
        thumb->SetProperty("background-color", Swatch);
        HueSwatchShown[view.row->Cvar] = Swatch;
      }
    }
  }

  if (toggle) {
    const bool on =
        view.state == services::settings::RowState::kInvalid
            ? (view.row->Kind == services::settings::SettingKind::kNegate
                   ? view.engineValue < 0.0f
                   : isOn(view.engineValue))
            : toggleDraftOn(view);
    control->SetInnerRML(framework::TextUtil::escapeRml(
        translated(on ? "#Settings_On" : "#Settings_Off")));
  } else if (select) {
    // Options transfer through the widget's Add() (inline RML children
    // only become matchable on the next Update - the T12 pin). The set
    // is rebuilt only when it changed, and the live engine value is
    // always offerable (a downloaded model must stay selectable even if
    // the scan missed it).
    auto *selectControl =
        rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(control);
    std::vector<std::string> values;
    std::vector<std::string> labels;
    // Invalid enum text cannot be shown by a select; fall back to the
    // engine value so the widget keeps an honest selection.
    const std::string shownText =
        view.state == services::settings::RowState::kInvalid
            ? (view.row->Kind == services::settings::SettingKind::kEnum
                   ? numberText(view.engineValue)
                   : view.engineText)
            : view.draftText;
    // Empty values are legal options (the "unset" placeholder): the
    // widget auto-selects the first option when nothing matches, and a
    // skipped placeholder would make it commit a real option instead.
    const auto addOption = [&values, &labels](const std::string &value,
                                              const std::string &label) {
      if (std::find(values.begin(), values.end(), value) != values.end())
        return;
      values.push_back(value);
      labels.push_back(label);
    };
    if (view.row->Kind == services::settings::SettingKind::kEnum) {
      // The current/staged value is always offerable FIRST: the
      // localized scripts ship option sets that can omit the user's
      // live value (live-caught: hpk_maxsize engine 4 vs the schinese
      // options 0/2/16 - without the live entry the widget auto-picks
      // the first listed option and commits it as a draft). The
      // shipped options follow in table order.
      std::string ShownLabel = shownText;
      for (int I = 0; I < view.row->OptionCount; ++I)
        if (numberText(view.row->Options[I].Value) == shownText)
          ShownLabel = translated(view.row->Options[I].Label);
      addOption(shownText, ShownLabel);
      for (int i = 0; i < view.row->OptionCount; ++i)
        addOption(numberText(view.row->Options[i].Value),
                  translated(view.row->Options[i].Label));
    } else {
      // The live value rides FIRST (the widget auto-picks the first
      // option when it is added, and the deferred change it fires
      // then carries the live value = clean; the T12 resolution
      // select relies on the same ordering). An empty live value
      // gets an "unset" placeholder option so the widget still shows
      // something honest. The fixed token table (cl_logocolor) and
      // the scanned set (model / cl_logofile) follow.
      const auto StringLabel = [&](const std::string &Value) {
        if (view.row->StateName &&
            std::string(view.row->StateName) == "logocolor")
          return std::string(
              translated(("#Valve_" + logoColorName(Value)).c_str()));
        if (view.row->FileChoices)
          return fileStem(Value);
        return Value;
      };
      if (shownText.empty())
        addOption("", translated("#Settings_Unset"));
      else
        addOption(shownText, StringLabel(shownText));
      for (int i = 0; i < view.row->OptionCount; ++i)
        addOption(view.row->Options[i].Label,
                  StringLabel(view.row->Options[i].Label));
      if (view.options) {
        const std::vector<std::string> *optionLabels =
            view.optionLabels &&
                    view.optionLabels->size() == view.options->size()
                ? view.optionLabels
                : nullptr;
        for (size_t i = 0; i < view.options->size(); ++i) {
          const std::string &option = (*view.options)[i];
          const std::string label = optionLabels && !(*optionLabels)[i].empty()
                                        ? (*optionLabels)[i]
                                        : StringLabel(option);
          addOption(option, label);
        }
      }
    }
    if (selectControl) {
      const std::vector<std::string> *shown = shownSelectValues(view.row->Cvar);
      if (!shown || *shown != values) {
        selectControl->RemoveAll();
        for (size_t i = 0; i < values.size(); ++i)
          selectControl->Add(framework::TextUtil::escapeRml(labels[i]),
                             values[i]);
        setShownSelectValues(view.row->Cvar, values);
      }
      // Selection through SetSelection (a programmatic value attribute
      // WOULD dispatch change - the T12 stack-overflow trap).
      int index = -1;
      for (size_t i = 0; i < values.size(); ++i) {
        if (values[i] == shownText) {
          index = static_cast<int>(i);
          break;
        }
      }
      if (index >= 0 && selectControl->GetSelection() != index)
        selectControl->SetSelection(index);
    }
  } else if (!skipInput) {
    // The committed text is the projection; a programmatic SetValue does
    // not dispatch change (no feedback loop through the model). The origin
    // field of a change commit is skipped entirely so its caret
    // and IME state survive the status refresh.
    if (Rml::ElementFormControl *form =
            rmlui_dynamic_cast<Rml::ElementFormControl *>(control))
      form->SetValue(view.draftText.c_str());
  }

  // Row status: domain facts plus the pending-state suffix.
  std::string status;
  switch (view.row->Kind) {
  case services::settings::SettingKind::kFloat:
    status = view.row->Bounded
                 ? abc::Format("{0}-{1} {2} {3}", numberText(view.row->Min),
                               numberText(view.row->Max),
                               translated("#Settings_Default"),
                               numberText(view.row->DefaultValue))
                 : translated("#Settings_Default") + " " +
                       numberText(view.row->DefaultValue);
    break;
  case services::settings::SettingKind::kEnum:
    status = abc::Format(
        "{0} {1} | {2} {3}", std::to_string(view.row->OptionCount),
        translated("#Settings_Choices"), translated("#Settings_Default"),
        numberText(view.row->DefaultValue));
    break;
  case services::settings::SettingKind::kText:
    status = abc::Format(
        "{0} {1}",
        std::to_string(static_cast<int>(services::settings::kNameMaxBytes)),
        translated("#Settings_TextMax"));
    break;
  case services::settings::SettingKind::kNegate:
    status = translated("#Settings_NegateDomain");
    break;
  case services::settings::SettingKind::kStringEnum:
    status = translated("#Settings_EnumDomain");
    break;
  case services::settings::SettingKind::kCommandToggle:
    status = translated("#Settings_CommandDomain");
    break;
  case services::settings::SettingKind::kBool:
    status = translated("#Settings_BoolDomain");
    break;
  }
  if (!view.row->HasDefault)
    status = translated("#Settings_NoDefault");
  switch (view.state) {
  case services::settings::RowState::kDirty:
    status += " | " + translated("#Settings_Dirty");
    break;
  case services::settings::RowState::kInvalid:
    // The typed text is user input: it rides the status string and the
    // single escapeRml below makes it inert (no inner escaping here -
    // that would double-encode).
    status +=
        " | " + translated("#Settings_Invalid") + " (" + view.draftText + ")";
    break;
  case services::settings::RowState::kConflict:
    status +=
        " | " +
        abc::Format("{0} {1}", translated("#Settings_Conflict"),
                    view.row->Kind == services::settings::SettingKind::kText ||
                            view.row->Kind ==
                                services::settings::SettingKind::kStringEnum
                        ? view.engineText
                        : numberText(view.engineValue));
    break;
  case services::settings::RowState::kQueued:
    status += " | " + translated("#Settings_Queued");
    break;
  case services::settings::RowState::kApplyFailed:
    status += " | " + translated("#Settings_ApplyFailed");
    break;
  case services::settings::RowState::kClean:
  case services::settings::RowState::kMissing:
    break;
  }
  if (view.row->ReadOnly)
    status = translated("#Settings_ReadOnly");
  if (Rml::Element *statusNode = rowNode->QuerySelector(".row-status"))
    statusNode->SetInnerRML(framework::TextUtil::escapeRml(status));
}

const std::vector<std::string> *
SettingsPage::shownSelectValues(const std::string &cvar) const {
  const auto itor = SelectValuesShown.find(cvar);
  return itor == SelectValuesShown.end() ? nullptr : &itor->second;
}

void SettingsPage::setShownSelectValues(const std::string &cvar,
                                        std::vector<std::string> values) {
  SelectValuesShown[cvar] = std::move(values);
}

void SettingsPage::setStatusSummary() {
  Rml::ElementDocument *doc = document();
  if (!doc || !Model)
    return;
  Rml::Element *status = doc->GetElementById("settings-status");
  if (!status)
    return;
  std::string text;
  if (!Model->isDirty()) {
    text = translated("#Settings_StatusClean");
  } else {
    text = translated("#Settings_StatusDirty");
    if (Model->hasConflicts())
      text += " | " + translated("#Settings_StatusConflict");
  }
  status->SetInnerRML(framework::TextUtil::escapeRml(text));
}
} // namespace panels::settings
