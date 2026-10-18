// Extracted from SettingsPage.cpp (UTF-8). Responsibility:
// sections/BindingRows.
#include "panels/settings/SettingsUi.h"
#include "panels/settings/SettingsPage.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include "services/settings/SettingsModel.h"
#include "services/settings/KeyBindModel.h"
#include "framework/TextUtil.h"
#include "framework/Localization.h"
#include <string>
namespace panels::settings {
using namespace detail;

namespace {

// kb_act.lst delimits its sections with "blank" rows whose label is the
// vanilla file's ASCII rule ("==========================="). Those rows are
// file typography, not content: this theme renders the real titles only, so
// the rules (and any label-less divider) are dropped from the skeleton.
bool bindHeaderIsRule(const services::settings::BindRowView &view) {
  if (!view.action)
    return true;
  for (char c : view.action->Label) {
    if (c != '=' && c != ' ' && c != '\t')
      return false;
  }
  return true; // a pure run of '=' (or nothing at all)
}

} // namespace

void SettingsPage::buildBindRows() {
  if (!BindModel || !document())
    return;
  Rml::Element *rows = document()->GetElementById("bindrows");
  if (!rows)
    return;
  // One skeleton block per kb_act entry: section headers render as plain
  // dividers, actions carry label + key list + bind/clear buttons. The DOM
  // id is the action index (a plain number).
  std::string rml;
  for (const auto &view : BindModel->rows()) {
    if (view.header) {
      if (bindHeaderIsRule(view))
        continue;
      rml +=
          "<div class=\"bind-header\"><span>" +
          framework::TextUtil::escapeRml(bindActionLabel(view)) +
          "</span><span class=\"bind-header-columns\">" +
          framework::TextUtil::escapeRml(translated("#Settings_BindColumns")) +
          "</span></div>";
      continue;
    }
    rml += "<div class=\"bind-row\" id=\"bindrow-";
    rml += std::to_string(view.actionIndex);
    rml += "\"><div class=\"bind-line\">";
    rml += "<div class=\"bind-label\"></div>";
    rml += "<div class=\"bind-keys\"></div>";
    rml += "<button class=\"bind-btn bind-capture-btn\" "
           "data-action=\"bind-capture\" data-row=\"";
    rml += std::to_string(view.actionIndex);
    rml += "\"/>";
    rml += "<button class=\"bind-btn bind-clear-btn\" "
           "data-action=\"bind-clear\" data-row=\"";
    rml += std::to_string(view.actionIndex);
    rml += "\"/>";
    rml += "</div>";
    rml += "<div class=\"bind-status\"></div></div>";
  }
  rows->SetInnerRML(rml);
}

std::string SettingsPage::bindActionLabel(
    const services::settings::BindRowView &view) const {
  if (!view.action)
    return {};
  return bindLabelText(view.action->Label);
}

std::string SettingsPage::bindLabelText(const std::string &label) const {
  if (!label.empty() && label[0] == '#') {
    std::string out;
    if (framework::Localization::translate(out, label.substr(1)))
      return out;
    if (BindModel && BindModel->lookupLabel(label.substr(1), out))
      return out;
    return label;
  }
  return label;
}

void SettingsPage::renderBindRow(const services::settings::BindRowView &view) {
  Rml::ElementDocument *doc = document();
  if (!doc || !view.action)
    return;
  Rml::Element *rowNode = doc->GetElementById(
      (std::string("bindrow-") + std::to_string(view.actionIndex)).c_str());
  if (!rowNode)
    return;
  // The capturing row highlights its key list (class, not a display
  // property - the color lands immediately).
  const bool capturing = BindModel && BindModel->capturing() &&
                         BindModel->captureRow() == view.actionIndex;
  rowNode->SetClass("bind-capturing", capturing);
  if (Rml::Element *label = rowNode->QuerySelector(".bind-label"))
    label->SetInnerRML(framework::TextUtil::escapeRml(bindActionLabel(view)));

  // Keys in draft order; "unbound" placeholder keeps the row readable.
  std::string keys;
  for (size_t i = 0; i < view.draftKeyNames.size(); ++i) {
    if (i)
      keys += ", ";
    keys += view.draftKeyNames[i];
  }
  if (view.draftKeyNames.empty())
    keys = translated("#Settings_BindNone");
  if (Rml::Element *keysNode = rowNode->QuerySelector(".bind-keys"))
    keysNode->SetInnerRML(framework::TextUtil::escapeRml(keys));

  // Buttons: capture text flips while this row is capturing; clear is the
  // unbind-all-keys affordance of the row (drafts only until Apply).
  if (Rml::Element *button = rowNode->QuerySelector(".bind-capture-btn"))
    button->SetInnerRML(framework::TextUtil::escapeRml(translated(
        capturing ? "#Settings_BindCancelCapture" : "#Settings_BindCapture")));
  if (Rml::Element *button = rowNode->QuerySelector(".bind-clear-btn"))
    button->SetInnerRML(
        framework::TextUtil::escapeRml(translated("#Settings_BindClear")));

  std::string status;
  if (capturing) {
    status = translated("#Settings_BindPressKey");
  } else {
    switch (view.state) {
    case services::settings::BindRowState::kDirty:
      status = translated("#Settings_BindDirty");
      break;
    case services::settings::BindRowState::kQueued:
      status = translated("#Settings_BindQueued");
      break;
    case services::settings::BindRowState::kApplyFailed:
      status = translated("#Settings_BindFailed");
      break;
    case services::settings::BindRowState::kUnavailable:
      status = translated("#Settings_BindUnavailable");
      break;
    case services::settings::BindRowState::kClean:
      break;
    }
  }
  if (Rml::Element *statusNode = rowNode->QuerySelector(".bind-status"))
    statusNode->SetInnerRML(framework::TextUtil::escapeRml(status));
}

void SettingsPage::renderBindConflict() {
  Rml::ElementDocument *doc = document();
  if (!doc || !BindModel)
    return;
  Rml::Element *layer = doc->GetElementById(kLayerBindConflict);
  if (!layer)
    return;
  if (BindModel->conflictPending()) {
    // The keyname and the other action's label ride the layer text, each
    // piece escaped on its own (the label may carry user-facing engine
    // strings; nothing here is ever injected unescaped).
    if (Rml::Element *node = doc->GetElementById("bind-conflict-text"))
      node->SetInnerRML(
          framework::TextUtil::escapeRml(
              translated("#Settings_BindConflictLead")) +
          " " + framework::TextUtil::escapeRml(BindModel->conflictKeyName()) +
          " " +
          framework::TextUtil::escapeRml(translated("#Settings_BindBoundTo")) +
          " " +
          framework::TextUtil::escapeRml(
              bindLabelText(BindModel->conflictOtherLabel())) +
          "?");
    if (ActiveLayer != kLayerBindConflict)
      showLayer(kLayerBindConflict);
  } else if (ActiveLayer == kLayerBindConflict) {
    hideLayers();
  }
}

void SettingsPage::setBindStatusSummary() {
  Rml::ElementDocument *doc = document();
  if (!doc)
    return;
  Rml::Element *status = doc->GetElementById("bind-status");
  if (!status)
    return;
  std::string text;
  if (!BindModel || !BindModel->readsAvailable()) {
    text = translated("#Settings_BindUnavailable");
  } else if (BindModel->conflictPending()) {
    text = translated("#Settings_BindConflictSummary");
  } else if (!BindModel->isDirty()) {
    text = translated("#Settings_BindClean");
  } else {
    text = translated("#Settings_BindDirtySummary");
  }
  status->SetInnerRML(framework::TextUtil::escapeRml(text));
}
} // namespace panels::settings
