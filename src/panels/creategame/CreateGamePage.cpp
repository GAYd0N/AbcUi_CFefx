// panels::creategame implementation - see CreateGamePage.h.
#include "CreateGamePage.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/StringUtilities.h>

#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"

#include <cstdio>
#include <cstdlib>

namespace panels::creategame {

namespace {

constexpr const char *kActionApply = "cg-apply";
constexpr const char *kActionCancel = "cg-cancel";

Rml::Element *actionNode(Rml::Element *node) {
  for (Rml::Element *it = node; it; it = it->GetParentNode()) {
    if (it->HasAttribute("data-action"))
      return it;
  }
  return nullptr;
}

// Local table first (project "#CG_*" tokens), then the raw text - an unknown
// token is shown instead of a blank line (honest fallback).
std::string resolve(const std::string &text) {
  if (text.empty())
    return {};
  if (text[0] != '#')
    return text;
  std::string out;
  if (framework::Localization::translate(out, text.substr(1)))
    return out;
  return text;
}

} // namespace

CreateGamePage::CreateGamePage()
    : InteractiveDocument("menucreategame",
                          "abc/abcui/gameui/rml/creategame.rml", true),
      Listener(this) {}

CreateGamePage::~CreateGamePage() {
  close();
}

void CreateGamePage::onDocumentReady() {
  if (Rml::ElementDocument *doc = document()) {
    addListener(doc, "click", &Listener);
    addListener(doc, "change", &Listener);
  }
}

Rml::Element *CreateGamePage::node(const char *id) const {
  Rml::ElementDocument *doc = document();
  return doc ? doc->GetElementById(id) : nullptr;
}

void CreateGamePage::prepare(const services::creategame::MapCatalog &catalog,
                             const char *defaultName) {
  Applied = false;
  Lan = false;
  buildOptions();
  Maps.clear();
  for (std::size_t i = 0; i < catalog.size(); ++i)
    Maps.push_back(catalog.nameAt(i));

  if (auto *Filter =
          rmlui_dynamic_cast<Rml::ElementFormControl *>(node("cg-map-filter")))
    Filter->SetValue("");
  filterMaps("");

  if (Rml::ElementFormControl *control =
          rmlui_dynamic_cast<Rml::ElementFormControl *>(node("cg-players")))
    control->SetValue("8"); // settings.scr default (NUMBER 1..32)
  if (Rml::ElementFormControl *control =
          rmlui_dynamic_cast<Rml::ElementFormControl *>(node("cg-name"))) {
    std::string name = defaultName ? defaultName : "";
    if (name.empty())
      name = resolve("#CG_DefaultName");
    control->SetValue(name);
  }
  if (Rml::ElementFormControl *control =
          rmlui_dynamic_cast<Rml::ElementFormControl *>(node("cg-password")))
    control->SetValue("");

  showHint(nullptr);
  if (auto *Control = node("cg-players-value"))
    Control->SetInnerRML("8");
}

void CreateGamePage::filterMaps(const std::string &Query) {
  auto *Select =
      rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(node("cg-map"));
  if (!Select)
    return;
  const auto Previous = Select->GetValue();
  const auto Needle = Rml::StringUtilities::ToLower(Query);
  Select->RemoveAll();
  int Selected = 0;
  int Matches = 0;
  std::string Rows;
  for (const auto &Map : Maps) {
    if (Rml::StringUtilities::ToLower(Map).find(Needle) == std::string::npos)
      continue;
    if (Map == Previous)
      Selected = Select->GetNumOptions();
    Select->Add(framework::TextUtil::escapeRml(Map), Map);
    // The row list mirrors the option list one-to-one (data-index is the
    // option index), so a pick can never name a map outside the filtered
    // set and the highlight has a single source of truth.
    Rows +=
        "<div class=\"cg-map-row\" data-action=\"cg-map-pick\" data-index=\"";
    Rows += std::to_string(Matches);
    Rows += "\"><span class=\"cg-map-row-name\">";
    Rows += framework::TextUtil::escapeRml(Map);
    Rows += "</span></div>";
    ++Matches;
  }
  Select->SetSelection(Select->GetNumOptions() ? Selected : -1);
  Select->SetValue(Select->GetNumOptions() ? Select->GetValue() : "");
  if (Rml::Element *List = node("cg-map-list"))
    List->SetInnerRML(Rows);
  if (Rml::Element *Count = node("cg-map-count")) {
    char Text[48];
    _snprintf_s(Text, sizeof(Text), _TRUNCATE, "%d / %d", Matches,
                static_cast<int>(Maps.size()));
    Count->SetInnerRML(Text);
  }
  highlightSelectedMap();
  if (auto *Apply = node("cg-apply")) {
    if (Select->GetNumOptions())
      Apply->RemoveAttribute("disabled");
    else
      Apply->SetAttribute("disabled", "disabled");
  }
}

void CreateGamePage::highlightSelectedMap() {
  Rml::Element *List = node("cg-map-list");
  auto *Select =
      rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(node("cg-map"));
  if (!List || !Select)
    return;
  const int Active = Select->GetSelection();
  for (int i = 0; i < List->GetNumChildren(); ++i) {
    Rml::Element *Row = List->GetChild(i);
    const bool IsActive = Row->GetAttribute<int>("data-index", -1) == Active;
    Row->SetClass("cg-map-row-active", IsActive);
    if (!IsActive)
      continue;
    // Keep the selection visible without re-aligning a row the player can
    // already see (a filter rebuild can select a row far down the list).
    const float RowTop = Row->GetAbsoluteTop();
    const float RowBottom = RowTop + Row->GetOffsetHeight();
    const float ViewTop = List->GetClientTop();
    const float ViewBottom = ViewTop + List->GetClientHeight();
    if (RowTop < ViewTop)
      Row->ScrollIntoView(true);
    else if (RowBottom > ViewBottom)
      Row->ScrollIntoView(false);
  }
}

void CreateGamePage::showHint(const char *text) {
  if (text && *text)
    Applied = false; // rejected form can be corrected, successful launches stay
                     // latched
  Rml::Element *hint = node("cg-hint");
  if (!hint)
    return;
  const std::string resolved = text ? resolve(text) : std::string();
  hint->SetInnerRML(framework::TextUtil::escapeRml(resolved));
  hint->SetProperty("display", resolved.empty() ? "none" : "block");
}

std::string CreateGamePage::selectedMap() const {
  if (Rml::ElementFormControlSelect *select =
          rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(
              const_cast<Rml::Element *>(node("cg-map"))))
    return select->GetValue();
  return {};
}

std::string CreateGamePage::playersText() const {
  if (Rml::ElementFormControlInput *input =
          rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
              const_cast<Rml::Element *>(node("cg-players"))))
    return std::to_string(
        static_cast<int>(std::strtof(input->GetValue().c_str(), nullptr)));
  return {};
}

std::string CreateGamePage::serverName() const {
  if (Rml::ElementFormControlInput *input =
          rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
              const_cast<Rml::Element *>(node("cg-name"))))
    return input->GetValue();
  return {};
}

std::string CreateGamePage::password() const {
  if (Rml::ElementFormControlInput *input =
          rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
              const_cast<Rml::Element *>(node("cg-password"))))
    return input->GetValue();
  return {};
}

void CreateGamePage::refocusDefault() {
  // The search field is the picker's first control: typing narrows the list
  // immediately, and the row list is one Tab away.
  if (Rml::Element *filter = node("cg-map-filter"))
    filter->Focus();
}

void CreateGamePage::handleClick(Rml::Event &event) {
  Rml::Element *entry = actionNode(event.GetTargetElement());
  if (!entry)
    return;
  const Rml::String action =
      entry->GetAttribute<Rml::String>("data-action", "");
  if (action.empty())
    return;
  if (action == "cg-map-pick") {
    // A row pick moves the select's selection (the list and the select are
    // index-parallel); the highlight follows. No action is reported - the
    // map choice stays page-local until Apply assembles the request.
    auto *Select =
        rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(node("cg-map"));
    const int Index = entry->GetAttribute<int>("data-index", -1);
    if (Select && Index >= 0 && Index < Select->GetNumOptions()) {
      Select->SetSelection(Index);
      highlightSelectedMap();
    }
    return;
  }
  if (action == "cg-lan") {
    Lan = !Lan;
    entry->SetInnerRML(resolve(Lan ? "#Settings_On" : "#Settings_Off"));
    return;
  }
  if (action == kActionApply) {
    if (Applied)
      return; // one report per opening (the backend latch is behind it)
    Applied = true;
  } else if (action != kActionCancel) {
    return;
  }
  const ActionSink Action = Sink;
  if (Action)
    Action(action.c_str());
}

void CreateGamePage::PageListener::ProcessEvent(Rml::Event &event) {
  if (event.GetType() == "change")
    Owner->handleChange(event);
  else
    Owner->handleClick(event);
}

} // namespace panels::creategame
