// panels::playerlist implementation - see PlayerListPage.h.
#include "PlayerListPage.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"

#include <cstdio>

namespace panels {
namespace playerlist {

namespace {

constexpr const char *kActionSelect = "pl-row";
constexpr const char *kActionMute = "pl-mute";
constexpr const char *kActionBack = "pl-close";

Rml::Element *actionNode(Rml::Element *node) {
  for (Rml::Element *it = node; it; it = it->GetParentNode()) {
    if (it->HasAttribute("data-action"))
      return it;
  }
  return nullptr;
}

std::string resolve(const char *token) {
  std::string out;
  if (token && framework::Localization::translate(out, token + 1))
    return out;
  return token ? token : "";
}

} // namespace

PlayerListPage::PlayerListPage()
    : InteractiveDocument("menuplayerlist",
                          "abc/abcui/gameui/rml/playerlist.rml", true),
      Listener(this) {
  Self = this;
}

PlayerListPage::~PlayerListPage() {
  close();
}

void PlayerListPage::onDocumentReady() {
  if (Rml::ElementDocument *doc = document())
    addListener(doc, "click", &Listener);
}

Rml::Element *PlayerListPage::node(const char *id) const {
  Rml::ElementDocument *doc = document();
  return doc ? doc->GetElementById(id) : nullptr;
}

void PlayerListPage::render() {
  Rml::ElementDocument *doc = document();
  if (!doc || !Model)
    return;
  const std::size_t revision = Model->revision();
  if (revision == RenderedRevision)
    return; // nothing moved since the last projection
  RenderedRevision = revision;

  Rml::Element *list = doc->GetElementById("pl-list");
  if (!list)
    return;
  std::string RowsRml;

  const auto &roster = Model->roster();
  const auto &selection = Model->selection();
  // The shipped empty-state label lives on the ListPanel
  // (#GameUI_NoOtherPlayersInGame); the same here when the roster holds
  // only the local player (the shipped dialog listed every non-empty name
  // including your own row).
  int rows = 0;
  for (const auto &entry : roster) {
    const bool selected = selection.matches(entry);
    std::string state;
    if (entry.local)
      state = resolve("#PL_You");
    else if (entry.bot)
      state = resolve("#PL_Bot");
    if (entry.muted)
      state += (state.empty() ? "" : " ") + resolve("#PL_MutedLabel");
    else if (entry.spectating)
      state += (state.empty() ? "" : " ") + resolve("#PL_Spectating");

    std::string rml = "<div class=\"pl-row";
    if (selected)
      rml += " pl-selected";
    rml += "\" data-action=\"pl-row\" data-index=\"";
    rml += std::to_string(entry.index);
    rml += "\"";
    if (entry.steamId64 != 0) {
      char id[32];
      _snprintf_s(id, sizeof(id), _TRUNCATE, "%llu",
                  static_cast<unsigned long long>(entry.steamId64));
      rml += " data-steamid=\"";
      rml += id;
      rml += "\"";
    }
    rml += " data-name=\"";
    rml += framework::TextUtil::escapeRmlAttribute(entry.name);
    rml += "\">";
    rml += "<span class=\"pl-name\">";
    rml += framework::TextUtil::escapeRml(entry.name);
    rml += "</span>";
    if (!state.empty()) {
      rml += "<span class=\"pl-state\">";
      rml += framework::TextUtil::escapeRml(state);
      rml += "</span>";
    }
    rml += "</div>";
    RowsRml += rml;
    ++rows;
  }

  if (rows == 0)
    RowsRml = "<p id=\"pl-empty\">" +
              framework::TextUtil::escapeRml(resolve("#PL_Empty")) + "</p>";
  list->SetInnerRML(RowsRml);

  // Mute control: label follows the selected player's ACTUAL backend state
  // (a read-through, never an optimistic flip); enabled only when the model
  // can actually act (backend present, mutable selection) - the shipped
  // enable rule.
  Rml::Element *mute = doc->GetElementById("pl-mute");
  if (mute) {
    const auto *selected = Model->selectedEntry();
    const bool mutableSelection = selected != nullptr &&
                                  selected->steamId64 != 0 && !selected->bot &&
                                  Model->muteAvailable();
    std::string label;
    if (!Model->muteAvailable())
      label = resolve("#PL_MuteUnavailable");
    else if (selected && selected->muted)
      label = resolve("#PL_Unmute");
    else
      label = resolve("#PL_Mute");
    if (Rml::Element *text = mute->GetElementById("pl-mute-label"))
      text->SetInnerRML(framework::TextUtil::escapeRml(label));
    else
      mute->SetInnerRML(framework::TextUtil::escapeRml(label));
    mute->SetClass("pl-disabled", !mutableSelection);
    mute->SetAttribute("data-action", mutableSelection ? "pl-mute" : "");
  }

  Rml::Element *status = doc->GetElementById("pl-status");
  if (status) {
    std::string line =
        Model->muteAvailable() ? "" : resolve("#PL_MuteUnavailable");
    status->SetInnerRML(framework::TextUtil::escapeRml(line));
    status->SetProperty("display", line.empty() ? "none" : "block");
  }
}

void PlayerListPage::refocusDefault() {
  if (Rml::Element *list = node("pl-list")) {
    if (list->GetNumChildren() > 0 &&
        list->GetFirstChild()->GetTagName() == "div") {
      list->GetFirstChild()->Focus();
      return;
    }
  }
  if (Rml::Element *close = node("pl-close"))
    close->Focus();
}

void PlayerListPage::handleClick(Rml::Event &event) {
  Rml::Element *entry = actionNode(event.GetTargetElement());
  if (!entry)
    return;
  const Rml::String action =
      entry->GetAttribute<Rml::String>("data-action", "");
  if (action.empty())
    return;
  // DOM attributes are decoded once here. Identity crosses the module
  // boundary as a value, not a colon-delimited/truncated command string.
  if (action == kActionSelect) {
    services::playerlist::RosterEntry Row;
    Row.index = entry->GetAttribute<int>("data-index", 0);
    Row.steamId64 = entry->GetAttribute<unsigned long long>("data-steamid", 0);
    Row.name = entry->GetAttribute<Rml::String>("data-name", "");
    const SelectionSink Handler = SelectionHandler;
    if (Handler)
      Handler(services::playerlist::SelectionKey::forEntry(Row));
    return;
  }
  const ActionSink Action = Sink;
  if (Action)
    Action(action.c_str());
}

void PlayerListPage::PageListener::ProcessEvent(Rml::Event &event) {
  Owner->handleClick(event);
}

} // namespace playerlist
} // namespace panels
