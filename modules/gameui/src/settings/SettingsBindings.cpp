// The key-binding card: the action skeleton, the per-row draft projection,
// the conflict layer and the binding status summary.

#include "../shared/PageShared.h"
#include "SettingsInternal.h"

#include <string>

namespace settingspage {

// ----------------------------------------------------------- bindings -----

// kb_act.lst delimits its sections with "blank" rows whose label is the
// vanilla file's ASCII rule ("==========================="). Those rows are
// file typography, not content: this theme renders the real titles only, so
// the rules (and any label-less divider) are dropped from the skeleton.
bool bindHeaderIsRule(const AbcUiBindAction &row) {
  for (const char *c = row.label; *c; ++c) {
    if (*c != '=' && *c != ' ' && *c != '\t')
      return false;
  }
  return true;
}

// Shared resolution for one raw label string ("#Valve_X" or literal).
std::string bindLabelText(const std::string &label) {
  if (label.empty() || label[0] != '#')
    return label;
  const std::string token = label.substr(1);
  std::string text = translate(token.c_str());
  if (text != token)
    return text;
  char buffer[ABCUI_SETTINGS_LABEL_MAX];
  buffer[0] = '\0';
  if (Api->settings &&
      Api->settings->bindLookupLabel(token.c_str(), buffer, sizeof(buffer)))
    return buffer;
  return label;
}

std::string bindActionLabel(const AbcUiBindAction &row) {
  return bindLabelText(row.label);
}

void buildBindRows() {
  if (!Page || !Api->settings)
    return;
  AbcUiElement *rows = node("bindrows");
  if (!rows)
    return;
  // One skeleton block per kb_act entry: section headers render as plain
  // dividers, actions carry label + key list + bind/clear buttons. The DOM
  // id is the action index (a plain number).
  std::string markup;
  const int32_t count = Api->settings->bindRowCount();
  for (int32_t index = 0; index < count; ++index) {
    AbcUiBindAction row{};
    row.structSize = sizeof(row);
    if (!Api->settings->bindRowAt(index, &row))
      continue;
    if (row.header) {
      if (bindHeaderIsRule(row))
        continue;
      markup += "<div class=\"bind-header\"><span>" +
                escapeText(bindActionLabel(row)) +
                "</span><span class=\"bind-header-columns\">" +
                escapeText(resolve("#abcui.gameui/Settings_BindColumns")) +
                "</span></div>";
      continue;
    }
    const std::string id = std::to_string(row.index);
    markup += "<div class=\"bind-row\" id=\"bindrow-" + id +
              "\"><div class=\"bind-line\"><div class=\"bind-label\" "
              "id=\"bindrow-" +
              id + "-label\"></div><div class=\"bind-keys\" id=\"bindrow-" +
              id +
              "-keys\"></div><button class=\"bind-btn bind-capture-btn\" "
              "id=\"bindrow-" +
              id + "-capture\" data-action=\"" + kActionBindCapture +
              "\" data-row=\"" + id +
              "\"></button><button class=\"bind-btn bind-clear-btn\" "
              "id=\"bindrow-" +
              id + "-clear\" data-action=\"" + kActionBindClear +
              "\" data-row=\"" + id +
              "\"></button></div><div "
              "class=\"bind-status\" id=\"bindrow-" +
              id + "-status\"></div></div>";
  }
  Api->ui->elementSetInnerRML(rows, markup.c_str());
}

void renderBindRow(const AbcUiBindAction &row) {
  if (!Page)
    return;
  const std::string id = std::to_string(row.index);
  AbcUiElement *rowNode = node("bindrow-" + id);
  if (!rowNode)
    return;
  // The capturing row highlights its key list (class, not a display
  // property - the color lands immediately).
  const bool capturing = Api->settings->bindCapturing() == row.index;
  Api->ui->elementSetClass(rowNode, "bind-capturing", capturing);
  setInnerRml(node("bindrow-" + id + "-label"),
              escapeText(bindActionLabel(row)));

  // Keys in draft order; "unbound" placeholder keeps the row readable.
  std::string keys;
  for (int32_t index = 0; index < row.keyCount; ++index) {
    char key[ABCUI_SETTINGS_KEY_MAX];
    key[0] = '\0';
    if (!Api->settings->bindKeyAt(row.index, index, key, sizeof(key)))
      continue;
    if (!keys.empty())
      keys += ", ";
    keys += key;
  }
  if (keys.empty())
    keys = resolve("#abcui.gameui/Settings_BindNone");
  setInnerRml(node("bindrow-" + id + "-keys"), escapeText(keys));

  // Buttons: capture text flips while this row is capturing; clear is the
  // unbind-all-keys affordance of the row (drafts only until Apply).
  setInnerRml(
      node("bindrow-" + id + "-capture"),
      escapeText(resolve(capturing ? "#abcui.gameui/Settings_BindCancelCapture"
                                   : "#abcui.gameui/Settings_BindCapture")));
  setInnerRml(node("bindrow-" + id + "-clear"),
              escapeText(resolve("#abcui.gameui/Settings_BindClear")));

  std::string status;
  if (capturing) {
    status = resolve("#abcui.gameui/Settings_BindPressKey");
  } else {
    switch (row.state) {
    case ABCUI_BIND_STATE_DIRTY:
      status = resolve("#abcui.gameui/Settings_BindDirty");
      break;
    case ABCUI_BIND_STATE_QUEUED:
      status = resolve("#abcui.gameui/Settings_BindQueued");
      break;
    case ABCUI_BIND_STATE_APPLY_FAILED:
      status = resolve("#abcui.gameui/Settings_BindFailed");
      break;
    case ABCUI_BIND_STATE_UNAVAILABLE:
      status = resolve("#abcui.gameui/Settings_BindUnavailable");
      break;
    case ABCUI_BIND_STATE_CLEAN:
      break;
    }
  }
  setInnerRml(node("bindrow-" + id + "-status"), escapeText(status));
}

void renderBindConflict() {
  if (!Page || !Api->settings)
    return;
  AbcUiElement *layer = node(kLayerBindConflict);
  if (!layer)
    return;
  if (Api->settings->bindConflictPending()) {
    // The keyname and the other action's label ride the layer text, each
    // piece escaped on its own (the label may carry user-facing engine
    // strings; nothing here is ever injected unescaped).
    char keyName[ABCUI_SETTINGS_KEY_MAX];
    char otherLabel[ABCUI_SETTINGS_LABEL_MAX];
    keyName[0] = '\0';
    otherLabel[0] = '\0';
    Api->settings->bindConflictKeyName(keyName, sizeof(keyName));
    Api->settings->bindConflictOtherLabel(otherLabel, sizeof(otherLabel));
    setInnerRml(node("bind-conflict-text"),
                escapeText(resolve("#abcui.gameui/Settings_BindConflictLead")) +
                    " " + escapeText(keyName) + " " +
                    escapeText(resolve("#abcui.gameui/Settings_BindBoundTo")) +
                    " " + escapeText(bindLabelText(otherLabel)) + "?");
    if (ActiveLayer != kLayerBindConflict)
      showLayer(kLayerBindConflict);
  } else if (ActiveLayer == kLayerBindConflict) {
    hideLayers();
  }
}

void setBindStatusSummary() {
  if (!Page || !Api->settings)
    return;
  std::string text;
  if (!Api->settings->bindReadsAvailable()) {
    text = resolve("#abcui.gameui/Settings_BindUnavailable");
  } else if (Api->settings->bindConflictPending()) {
    text = resolve("#abcui.gameui/Settings_BindConflictSummary");
  } else if (!Api->settings->bindDirty()) {
    text = resolve("#abcui.gameui/Settings_BindClean");
  } else {
    text = resolve("#abcui.gameui/Settings_BindDirtySummary");
  }
  setInnerRml(node("bind-status"), escapeText(text));
}

} // namespace settingspage
