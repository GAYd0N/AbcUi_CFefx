// The page-owned controls of the find-servers page (the 2026-10-15
// redesign): ESC subpage handling, the docked filter column, the quick
// refresh of the current page, the blacklist editor and the row menu's
// blacklist entries. Module intents travel through the sink instead (see
// ServerBrowserPage.cpp).
#include "ServerBrowserPage.h"
#include "framework/Localization.h"
#include "framework/TextUtil.h"
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Event.h>
#include <charconv>

namespace panels::serverbrowser {
namespace {
std::string label(const char *Token) {
  std::string Text;
  return framework::Localization::translate(Text, Token)
             ? Text
             : std::string("#") + Token;
}
Rml::Element *actionNode(Rml::Element *Node) {
  for (; Node; Node = Node->GetParentNode())
    if (Node->HasAttribute("data-action"))
      return Node;
  return nullptr;
}
int intValue(const Rml::String &Text, int Fallback) {
  int Value = 0;
  const auto R = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
  if (R.ec == std::errc() && R.ptr == Text.data() + Text.size())
    return Value;
  return Fallback;
}
} // namespace

// ESC on the page closes one subpage: the row menu first, then the detail
// window. False hands the press back to the module (leave the page).
bool ServerBrowserPage::closeSubpage() {
  if (MenuOpen) {
    closeContextMenu();
    refocusDefault();
    return true;
  }
  if (DetailsOpen) {
    closeDetails();
    refocusDefault();
    return true;
  }
  return false;
}

// Page-owned controls, resolved before any module intent is dispatched.
bool ServerBrowserPage::handleControls(Rml::Event &Event) {
  Rml::Element *Entry = actionNode(Event.GetTargetElement());
  if (!Entry || !List || !Query)
    return false;
  const auto Action = Entry->GetAttribute<Rml::String>("data-action", "");
  if (Action == "sb-filters-reset") {
    // Reset restores every filter to its default; the game identity
    // (gamedir) and the cvar-owned unresponsive rule stay. The module
    // re-issues the query so the master-side pairs follow.
    services::serverbrowser::ServerFilter Reset;
    Reset.gameDir = List->filter().gameDir;
    Reset.HideUnresponsive = List->filter().HideUnresponsive;
    List->setFilter(Reset);
    Projected = false;
    const ActionSink ApplyFilters = Sink;
    if (ApplyFilters)
      ApplyFilters("sb-filters-apply");
    return true;
  }
  if (Action == "sb-tab-blacklist") {
    List->setBlockedOnly(true);
    Query->cancelDetail();
    DetailsOpen = false;
    Projected = false;
    return true;
  }
  if (Action.rfind("sb-tab-", 0) == 0) {
    RuleStatus.clear();
    List->setBlockedOnly(false);
    return false; // fixed query intent dispatched by the regular handler
  }
  if (Action == "sb-block" || Action == "sb-block-ip" ||
      Action == "sb-rule-add" || Action == "sb-rule-remove") {
    if (!Blacklist)
      return true;
    bool Ok = false;
    if (Action == "sb-rule-remove")
      Ok = Blacklist->remove(
          Entry->GetAttribute<Rml::String>("data-address", ""));
    else if (Action == "sb-rule-add") {
      if (auto *Input = rmlui_dynamic_cast<Rml::ElementFormControl *>(
              document()->GetElementById("sb-blacklist-address")))
        Ok = Blacklist->add(Input->GetValue());
    } else if (const auto *Server = List->selectedEntry(*Query)) {
      services::serverbrowser::BlacklistRule Rule{
          Server->ip, static_cast<uint16_t>(Action == "sb-block-ip" ? 0
                                            : Server->connectionPort
                                                ? Server->connectionPort
                                                : Server->queryPort)};
      Ok = Blacklist->add(Rule.address(), Server->name);
    }
    RuleStatus = label(Ok ? "SB_BlacklistSaved" : "SB_BlacklistError");
    Projected = false;
    renderControls();
    return true;
  }
  return false;
}

// The docked filter column's change events. Text and ping filters apply
// client-side only; the three master-pair toggles report the change to the
// module (which re-issues the query). Every path re-checks the model value
// first: a projection write that echoes the same value is a no-op, so the
// select's own change echo cannot loop.
void ServerBrowserPage::handleChange(Rml::Event &Event) {
  if (!List || !Query || InControls || !Event.GetTargetElement())
    return;
  const auto Id = Event.GetTargetElement()->GetId();
  const auto Text = Event.GetParameter<Rml::String>("value", "");
  if (Text.size() > 256)
    return;
  services::serverbrowser::ServerFilter Filter = List->filter();
  bool Changed = false;
  bool MasterPair = false;
  if (Id == "sb-filter-name") {
    Changed = Filter.Name != Text.c_str();
    Filter.Name = Text.c_str();
  } else if (Id == "sb-filter-map") {
    Changed = Filter.Map != Text.c_str();
    Filter.Map = Text.c_str();
  } else if (Id == "sb-filter-ping") {
    const int Value = intValue(Text, Filter.MaxPing);
    if (Value < 0 || Value > 500)
      return;
    Changed = Filter.MaxPing != Value;
    Filter.MaxPing = Value;
  } else if (Id == "sb-filter-password") {
    const bool Value = Text == "1";
    Changed = Filter.NoPassword != Value;
    Filter.NoPassword = Value;
  } else if (Id == "sb-filter-empty" || Id == "sb-filter-full" ||
             Id == "sb-filter-secure") {
    const bool Value = Text == "1";
    if (Id == "sb-filter-empty") {
      Changed = Filter.hideEmpty != Value;
      Filter.hideEmpty = Value;
    } else if (Id == "sb-filter-full") {
      Changed = Filter.hideFull != Value;
      Filter.hideFull = Value;
    } else {
      Changed = Filter.secureOnly != Value;
      Filter.secureOnly = Value;
    }
    MasterPair = true;
  } else {
    return;
  }
  if (!Changed)
    return;
  List->setFilter(Filter);
  Projected = false;
  if (MasterPair) {
    // The master-side pair moved: the module re-issues the list query.
    const ActionSink Action = Sink;
    if (Action)
      Action(Id.c_str());
  }
}
} // namespace panels::serverbrowser
