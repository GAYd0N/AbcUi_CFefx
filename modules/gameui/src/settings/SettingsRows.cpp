// The generic cvar rows: skeleton markup (cards, previews, widget hosts) and
// the per-row projection for every setting kind the framework model exposes.

#include "../shared/PageShared.h"
#include "SettingsInternal.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <string_view>

namespace settingspage {

void AppendRowRow(const AbcUiSettingRow &row, std::string &markup) {
  const std::string cvar = row.cvar;
  markup += "<div class=\"row\" id=\"row-" + cvar +
            "\"><div class=\"row-label\" id=\"row-" + cvar + "-label\"></div>";
  if (isToggleKind(row.kind)) {
    markup += "<button class=\"row-toggle\" id=\"row-" + cvar +
              "-control\" data-action=\"" + kActionToggle + "\" data-row=\"" +
              escapeAttribute(cvar) + "\"></button>";
  } else if (isSelectKind(row.kind)) {
    // Options transfer through the widget Add() at render time (the
    // inline-option transfer is a next-Update delay); the skeleton only
    // carries the empty select.
    markup += "<select class=\"row-select\" id=\"row-" + cvar +
              "-control\" data-row=\"" + escapeAttribute(cvar) + "\"></select>";
  } else {
    const bool slider = row.kind == ABCUI_SET_KIND_FLOAT &&
                        (row.flags & ABCUI_SET_ROW_BOUNDED) &&
                        row.minValue < row.maxValue;
    markup += "<input class=\"row-input";
    // Hue rows carry the drag thumb's own colour swatch (the style sheet
    // swaps their hover/focus feedback onto the thumb border).
    if (row.flags & ABCUI_SET_ROW_HUE_SWATCH)
      markup += " row-hue";
    if (slider)
      markup +=
          " row-slider\" type=\"range\" min=\"" + numberText(row.minValue) +
          "\" max=\"" + numberText(row.maxValue) + "\" step=\"" +
          numberText(row.step > 0 ? row.step
                                  : (row.maxValue - row.minValue) / 1000) +
          "\" value=\"" + numberText(row.draftValue);
    else
      markup += "\" type=\"text";
    markup += "\" id=\"row-" + cvar + "-control\" data-row=\"" +
              escapeAttribute(cvar) + "\"/>";
  }
  markup += "<span class=\"row-value\" id=\"row-" + cvar +
            "-value\"></span><div class=\"row-status\" id=\"row-" + cvar +
            "-status\"></div></div>";
}

void projectWidgets() {
  if (!Page)
    return;
  AbcUiElement *host = node("settings-widget-host");
  if (!host)
    return;
  const int page = pageIndexOf(ActivePage);
  for (int i = 0; i < kWidgetBlockCount; ++i) {
    AbcUiElement *block = node(kWidgetBlocks[i].Element);
    if (!block)
      continue;
    AbcUiElement *target = host;
    if (page >= 0) {
      AbcUiSettingsPage definition{};
      if (pageAt(page, definition)) {
        for (int32_t section = 0; section < definition.sectionCount;
             ++section) {
          if (!sectionHasComponent(page, section, kWidgetBlocks[i].Component))
            continue;
          AbcUiSettingsSection value{};
          if (!sectionAt(page, section, value))
            break;
          const std::string suffix =
              kWidgetBlocks[i].RowFlow ? "-items" : "-widgets";
          if (AbcUiElement *container =
                  node("settings-card-" + std::string(definition.id) + "-" +
                       std::string(value.id) + suffix))
            target = container;
          break;
        }
      }
    }
    if (Api->ui->elementParent(block) == target)
      continue;
    Api->ui->elementAppendChild(target, block);
  }
}

void OpenCard(std::string &markup, const std::string &cardId,
              const std::string &titleToken, bool hasPreview, bool hasBlock,
              bool stacked, bool twoUp) {
  markup += "<div class=\"settings-card";
  if (hasPreview)
    markup += " has-preview";
  if (hasBlock && !hasPreview)
    markup += " has-block";
  if (stacked)
    markup += " has-side-stack";
  if (twoUp)
    markup += " layout-two-up";
  markup += "\" id=\"" + cardId + "\">";
  if (!titleToken.empty())
    markup += "<div class=\"settings-card-title section-header\">" +
              escapeText(resolve(titleToken.c_str())) + "</div>";
  if (hasPreview) {
    markup += "<div class=\"settings-card-body\"><div "
              "class=\"settings-card-items\" id=\"" +
              cardId + "-items\">";
  } else {
    markup += "<div class=\"settings-card-widgets\" id=\"" + cardId +
              "-widgets\"></div>";
    markup += "<div class=\"settings-card-items\" id=\"" + cardId + "-items\">";
  }
}

void CloseCard(std::string &markup, const std::string &cardId,
               bool hasPreview) {
  markup += "</div>";
  if (hasPreview)
    markup += "<div class=\"settings-card-widgets\" id=\"" + cardId +
              "-widgets\"></div></div>";
  markup += "</div>";
}

void buildRows() {
  if (!Page || !Api->settings)
    return;
  AbcUiElement *rows = node("rows");
  if (!rows)
    return;
  std::string markup;
  if (pageCount() == 0) {
    std::string lastSection;
    bool open = false;
    int cardIndex = 0;
    const int count = Api->settings->rowCount();
    for (int index = 0; index < count; ++index) {
      AbcUiSettingRow row{};
      if (!rowById(index, row))
        continue;
      if (row.section[0] && lastSection != row.section) {
        lastSection = row.section;
        if (open)
          CloseCard(markup, "", false);
        OpenCard(markup, "settings-card-legacy-" + std::to_string(cardIndex++),
                 lastSection, false, false, false, false);
        open = true;
      } else if (!open) {
        OpenCard(markup, "settings-card-legacy-" + std::to_string(cardIndex++),
                 "", false, false, false, false);
        open = true;
      }
      AppendRowRow(row, markup);
    }
    if (open)
      CloseCard(markup, "", false);
  } else {
    const int rowCount = Api->settings->rowCount();
    for (int32_t page = 0; page < pageCount(); ++page) {
      AbcUiSettingsPage definition{};
      if (!pageAt(page, definition))
        continue;
      std::vector<std::string> cvars;
      cvars.reserve(static_cast<std::size_t>(definition.cvarCount));
      for (int32_t index = 0; index < definition.cvarCount; ++index) {
        const std::string cvar = pageCvar(page, index);
        if (!cvar.empty())
          cvars.push_back(cvar);
      }
      markup += "<div class=\"settings-cvar-page\" id=\"settings-rows-" +
                std::string(definition.id) + "\">";
      for (int32_t section = 0; section < definition.sectionCount; ++section) {
        AbcUiSettingsSection declared{};
        if (!sectionAt(page, section, declared))
          continue;
        bool hasPreview = false;
        bool hasBlock = false;
        bool hasWidget = false;
        for (int32_t component = 0; component < declared.componentCount;
             ++component) {
          const std::string name = sectionComponent(page, section, component);
          for (int i = 0; i < kWidgetBlockCount; ++i) {
            if (name != kWidgetBlocks[i].Component)
              continue;
            hasWidget = true;
            if (kWidgetBlocks[i].Preview)
              hasPreview = true;
            else if (!kWidgetBlocks[i].RowFlow)
              hasBlock = true;
          }
        }
        const auto visibleRow = [&](const AbcUiSettingRow &row) {
          if (declared.id[0] && std::string_view(row.section) != declared.id)
            return false;
          if (std::find(cvars.begin(), cvars.end(), row.cvar) == cvars.end())
            return false;
          if (sectionHasComponent(page, section, "model-picker") &&
              std::string_view(row.cvar) == "model")
            return false;
          if (sectionHasComponent(page, section, kColorPickerComponent) &&
              ownsColorCvar(row.cvar))
            return false;
          return true;
        };
        int count = 0;
        for (int index = 0; index < rowCount; ++index) {
          AbcUiSettingRow row{};
          if (rowById(index, row) && visibleRow(row))
            ++count;
        }
        if (count == 0 && !hasWidget)
          continue;
        const bool twoUp = std::string_view(declared.layout) == "two-up";
        const bool stacked = !twoUp && hasPreview && hasBlock;
        const std::string cardId = "settings-card-" +
                                   std::string(definition.id) + "-" +
                                   std::string(declared.id);
        OpenCard(markup, cardId, declared.label, twoUp ? false : hasPreview,
                 twoUp ? false : hasBlock, stacked, twoUp);
        if (twoUp) {
          int inPair = 0;
          for (int index = 0; index < rowCount; ++index) {
            AbcUiSettingRow row{};
            if (!rowById(index, row) || !visibleRow(row))
              continue;
            if (inPair == 0)
              markup += "<div class=\"row-pair\">";
            AppendRowRow(row, markup);
            if (++inPair == 2) {
              markup += "</div>";
              inPair = 0;
            }
          }
          if (inPair != 0)
            markup += "</div>";
        } else {
          for (int index = 0; index < rowCount; ++index) {
            AbcUiSettingRow row{};
            if (rowById(index, row) && visibleRow(row))
              AppendRowRow(row, markup);
          }
        }
        CloseCard(markup, cardId, twoUp ? false : hasPreview);
      }
      markup += "</div>";
    }
  }
  Api->ui->elementSetInnerRML(rows, markup.c_str());
  Built = true;
}

void renderRow(AbcUiSettingRow &row, bool skipInput) {
  if (!Page)
    return;
  const std::string cvar = row.cvar;
  AbcUiElement *rowNode = node("row-" + cvar);
  if (!rowNode)
    return;
  setInnerRml(node("row-" + cvar + "-label"),
              escapeText(resolve(row.labelToken)));

  const bool toggle = isToggleKind(row.kind);
  const bool select = isSelectKind(row.kind);
  AbcUiElement *control = node("row-" + cvar + "-control");
  if (!control)
    return;
  const bool missing = (row.flags & ABCUI_SET_ROW_MISSING) != 0;
  const bool editable = (row.flags & ABCUI_SET_ROW_EDITABLE) != 0;
  Api->ui->elementSetClass(control, "row-missing", missing);
  Api->ui->elementSetClass(control, "row-readonly",
                           (row.flags & ABCUI_SET_ROW_READ_ONLY) != 0);
  Api->ui->elementSetDisabled(control, missing || !editable);

  if (missing) {
    // Unavailable: fail-closed presentation, no editable control path.
    const std::string text =
        row.requireCvar[0]
            ? resolve("#abcui.gameui/Settings_DependencyUnavailable") + " " +
                  std::string(row.requireCvar)
            : resolve("#abcui.gameui/Settings_Unavailable");
    setInnerRml(node("row-" + cvar + "-status"), escapeText(text));
    return;
  }

  if (AbcUiElement *value = node("row-" + cvar + "-value")) {
    // Only the bounded float rows carry a readout; an empty inline block
    // would wrap onto its own line inside the narrow card column and push
    // the status line away from its control.
    const bool shows =
        row.kind == ABCUI_SET_KIND_FLOAT && (row.flags & ABCUI_SET_ROW_BOUNDED);
    setProperty(value, "display", shows ? "inline-block" : "none");
    setInnerRml(value, shows ? escapeText(row.draftText) : std::string());
  }

  // Hue rows (topcolor / bottomcolor): the drag thumb shows the colour the
  // value represents. The engine reads 0..255 as palette-hue degrees, so the
  // same mapping paints the swatch; the write is skipped while the colour is
  // unchanged (a render per input event must not re-dirty the style tree).
  if ((row.flags & ABCUI_SET_ROW_HUE_SWATCH) &&
      row.kind == ABCUI_SET_KIND_FLOAT) {
    if (AbcUiElement *thumb = Api->ui->elementFindTag(control, "sliderbar")) {
      char swatch[32];
      Api->ui->formatHueColor(row.draftValue, swatch, sizeof(swatch));
      std::string stored;
      bool known = false;
      for (const auto &entry : HueShown) {
        if (entry.first != cvar)
          continue;
        stored = entry.second;
        known = true;
        break;
      }
      if (!known || stored != swatch) {
        setProperty(thumb, "background-color", swatch);
        bool replaced = false;
        for (auto &entry : HueShown) {
          if (entry.first != cvar)
            continue;
          entry.second = swatch;
          replaced = true;
          break;
        }
        if (!replaced)
          HueShown.emplace_back(cvar, swatch);
      }
    }
  }

  if (toggle) {
    const bool on =
        row.state == ABCUI_SET_STATE_INVALID
            ? (row.kind == ABCUI_SET_KIND_NEGATE ? row.engineValue < 0.0f
                                                 : isOn(row.engineValue))
            : toggleDraftOn(row);
    setInnerRml(control,
                escapeText(resolve(on ? "#abcui.gameui/Settings_On"
                                      : "#abcui.gameui/Settings_Off")));
  } else if (select) {
    // Options transfer through the widget's Add() (inline RML children only
    // become matchable on the next Update). The set is rebuilt only when it
    // changed, and the live engine value is always offerable (a downloaded
    // model must stay selectable even if the scan missed it).
    std::vector<std::string> values;
    std::vector<std::string> labels;
    // Invalid enum text cannot be shown by a select; fall back to the engine
    // value so the widget keeps an honest selection.
    const std::string shownText =
        row.state == ABCUI_SET_STATE_INVALID
            ? (row.kind == ABCUI_SET_KIND_ENUM ? numberText(row.engineValue)
                                               : std::string(row.engineText))
            : std::string(row.draftText);
    // Empty values are legal options (the "unset" placeholder): the widget
    // auto-selects the first option when nothing matches, and a skipped
    // placeholder would make it commit a real option instead.
    const auto addOption = [&values, &labels](const std::string &value,
                                              const std::string &label) {
      for (const auto &existing : values)
        if (existing == value)
          return;
      values.push_back(value);
      labels.push_back(label);
    };
    if (row.kind == ABCUI_SET_KIND_ENUM) {
      // The current/staged value is always offerable FIRST: the localized
      // scripts ship option sets that can omit the user's live value, and
      // without the live entry the widget auto-picks the first listed option
      // and commits it as a draft.
      std::string shownLabel = shownText;
      for (int32_t index = 0; index < row.optionCount; ++index) {
        float optionValue = 0.0f;
        char label[ABCUI_SETTINGS_LABEL_MAX];
        label[0] = '\0';
        if (!Api->settings->optionAt(cvar.c_str(), index, &optionValue, label,
                                     sizeof(label)))
          continue;
        if (numberText(optionValue) == shownText)
          shownLabel = resolve(label);
      }
      addOption(shownText, shownLabel);
      for (int32_t index = 0; index < row.optionCount; ++index) {
        float optionValue = 0.0f;
        char label[ABCUI_SETTINGS_LABEL_MAX];
        label[0] = '\0';
        if (!Api->settings->optionAt(cvar.c_str(), index, &optionValue, label,
                                     sizeof(label)))
          continue;
        addOption(numberText(optionValue), resolve(label));
      }
    } else {
      const auto stringLabel = [&row](const std::string &value) -> std::string {
        if (std::string_view(row.stateName) == "logocolor")
          return resolve(("#Valve_" + logoColorName(value)).c_str());
        if (row.flags & ABCUI_SET_ROW_FILE_CHOICES)
          return fileStem(value);
        return value;
      };
      // The live value rides FIRST (the widget auto-picks the first option
      // when it is added, and the deferred change it fires then carries the
      // live value = clean). An empty live value gets an "unset" placeholder
      // so the widget still shows something honest.
      if (shownText.empty())
        addOption("", resolve("#abcui.gameui/Settings_Unset"));
      else
        addOption(shownText, stringLabel(shownText));
      for (int32_t index = 0; index < row.optionCount; ++index) {
        float optionValue = 0.0f;
        char label[ABCUI_SETTINGS_LABEL_MAX];
        label[0] = '\0';
        if (!Api->settings->optionAt(cvar.c_str(), index, &optionValue, label,
                                     sizeof(label)))
          continue;
        addOption(label, stringLabel(label));
      }
      for (int32_t index = 0; index < row.choiceCount; ++index) {
        char value[ABCUI_SETTINGS_TEXT_MAX];
        char label[ABCUI_SETTINGS_LABEL_MAX];
        value[0] = '\0';
        label[0] = '\0';
        if (!Api->settings->choiceAt(cvar.c_str(), index, value, sizeof(value),
                                     label, sizeof(label)))
          continue;
        addOption(value, label[0] ? std::string(label) : stringLabel(value));
      }
    }
    std::vector<std::string> *shown = nullptr;
    for (auto &entry : SelectShown) {
      if (entry.first != cvar)
        continue;
      shown = &entry.second;
      break;
    }
    if (!shown || *shown != values) {
      Api->ui->elementSelectRemoveAll(control);
      for (std::size_t index = 0; index < values.size(); ++index) {
        const std::string label = escapeText(labels[index]);
        Api->ui->elementSelectAdd(control, label.c_str(),
                                  values[index].c_str());
      }
      if (shown)
        *shown = values;
      else
        SelectShown.emplace_back(cvar, values);
    }
    // Selection through SetSelection (a programmatic value attribute WOULD
    // dispatch change - the stack-overflow trap).
    int32_t selection = -1;
    for (std::size_t index = 0; index < values.size(); ++index) {
      if (values[index] == shownText) {
        selection = static_cast<int32_t>(index);
        break;
      }
    }
    if (selection >= 0 && Api->ui->elementSelectSelection(control) != selection)
      Api->ui->elementSelectSetSelection(control, selection);
  } else if (!skipInput) {
    // The committed text is the projection; a programmatic value write does
    // not dispatch change (no feedback loop). The origin field of a change
    // commit is skipped entirely so its caret and IME state survive.
    Api->ui->elementSetValue(control, row.draftText);
  }

  // Row status: domain facts plus the pending-state suffix.
  std::string status;
  switch (row.kind) {
  case ABCUI_SET_KIND_FLOAT:
    status = (row.flags & ABCUI_SET_ROW_BOUNDED)
                 ? numberText(row.minValue) + "-" + numberText(row.maxValue) +
                       " " + resolve("#abcui.gameui/Settings_Default") + " " +
                       numberText(row.defaultValue)
                 : resolve("#abcui.gameui/Settings_Default") + " " +
                       numberText(row.defaultValue);
    break;
  case ABCUI_SET_KIND_ENUM:
    status = std::to_string(row.optionCount) + " " +
             resolve("#abcui.gameui/Settings_Choices") + " | " +
             resolve("#abcui.gameui/Settings_Default") + " " +
             numberText(row.defaultValue);
    break;
  case ABCUI_SET_KIND_TEXT:
    status = std::to_string(kTextMaxBytes) + " " +
             resolve("#abcui.gameui/Settings_TextMax");
    break;
  case ABCUI_SET_KIND_NEGATE:
    status = resolve("#abcui.gameui/Settings_NegateDomain");
    break;
  case ABCUI_SET_KIND_STRING_ENUM:
    status = resolve("#abcui.gameui/Settings_EnumDomain");
    break;
  case ABCUI_SET_KIND_COMMAND_TOGGLE:
    status = resolve("#abcui.gameui/Settings_CommandDomain");
    break;
  case ABCUI_SET_KIND_BOOL:
    status = resolve("#abcui.gameui/Settings_BoolDomain");
    break;
  }
  if (!(row.flags & ABCUI_SET_ROW_HAS_DEFAULT))
    status = resolve("#abcui.gameui/Settings_NoDefault");
  switch (row.state) {
  case ABCUI_SET_STATE_DIRTY:
    status += " | " + resolve("#abcui.gameui/Settings_Dirty");
    break;
  case ABCUI_SET_STATE_INVALID:
    // The typed text is user input: it rides the status string and the
    // single escape below makes it inert (no inner escaping here - that
    // would double-encode).
    status += " | " + resolve("#abcui.gameui/Settings_Invalid") + " (" +
              std::string(row.draftText) + ")";
    break;
  case ABCUI_SET_STATE_CONFLICT:
    status += " | " + resolve("#abcui.gameui/Settings_Conflict") + " " +
              (row.kind == ABCUI_SET_KIND_TEXT ||
                       row.kind == ABCUI_SET_KIND_STRING_ENUM
                   ? std::string(row.engineText)
                   : numberText(row.engineValue));
    break;
  case ABCUI_SET_STATE_QUEUED:
    status += " | " + resolve("#abcui.gameui/Settings_Queued");
    break;
  case ABCUI_SET_STATE_APPLY_FAILED:
    status += " | " + resolve("#abcui.gameui/Settings_ApplyFailed");
    break;
  case ABCUI_SET_STATE_CLEAN:
  case ABCUI_SET_STATE_MISSING:
    break;
  }
  if (row.flags & ABCUI_SET_ROW_READ_ONLY)
    status = resolve("#abcui.gameui/Settings_ReadOnly");
  setInnerRml(node("row-" + cvar + "-status"), escapeText(status));
}

void setStatusSummary() {
  if (!Page || !Api->settings)
    return;
  std::string text;
  if (!Api->settings->dirty()) {
    text = resolve("#abcui.gameui/Settings_StatusClean");
  } else {
    text = resolve("#abcui.gameui/Settings_StatusDirty");
    if (Api->settings->hasConflicts())
      text += " | " + resolve("#abcui.gameui/Settings_StatusConflict");
  }
  setInnerRml(node("settings-status"), escapeText(text));
}

// ------------------------------------------------------- crosshair colors --

} // namespace settingspage
