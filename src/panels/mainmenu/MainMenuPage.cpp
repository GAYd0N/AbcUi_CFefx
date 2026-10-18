// panels::mainmenu implementation - see MainMenuPage.h.
#include "MainMenuPage.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/ElementDocument.h>

#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"

#include <cstdlib>

namespace panels::mainmenu {

namespace {

// data-label attributes carry "#token" form like text nodes do; attributes
// skip Rml's translate hook, so resolve through the framework table here.
std::string resolveLabel(std::string label) {
  if (!label.empty() && label[0] == '#') {
    std::string out;
    if (framework::Localization::translate(out, label.substr(1)))
      return out;
  }
  return label;
}

// Nearest ancestor (or self) carrying a data-action; clicks delegated on the
// document resolve their intent here, so clicks landing on inner text still
// find the entry.
Rml::Element *actionNode(Rml::Element *node) {
  for (Rml::Element *it = node; it; it = it->GetParentNode()) {
    if (it->HasAttribute("data-action"))
      return it;
  }
  return nullptr;
}

// "#token" through the language table; an unknown token stays verbatim.
std::string localized(const char *value) {
  if (!value)
    return {};
  if (value[0] != '#')
    return value;
  std::string out;
  if (framework::Localization::translate(out, value + 1))
    return out;
  return value;
}

} // namespace

MainMenuPage::MainMenuPage(const char *documentPath)
    : InteractiveDocument("mainmenu", documentPath, true), Listener(this) {}

MainMenuPage::~MainMenuPage() {
  // Rml calls OnDetach: the member listener must still be alive here.
  close();
}

void MainMenuPage::onDocumentReady() {
  buildEntries();
  // One click binding for the whole document (bubbling); focus/blur do not
  // bubble (RmlUi event spec), so every data-label element carries its own
  // pair and the status line follows whichever control holds focus.
  if (Rml::ElementDocument *doc = document())
    addListener(doc, "click", &Listener);
  if (Rml::ElementDocument *doc = document())
    addListener(doc, "mousedown", &Listener); // right-click = button 1
  if (Rml::Element *root = document())
    attachFocusBindings(root);
  // The shipped shell starts on the no-map section; a section switch
  // through showSection re-renders anyway.
  if (Rml::Element *main = document()->GetElementById("page-main")) {
    if (Rml::Element *pause = document()->GetElementById("page-pause"))
      pause->SetProperty("display", "none");
  }
}

void MainMenuPage::buildEntries() {
  using framework::TextUtil::escapeRml;
  using framework::TextUtil::escapeRmlAttribute;
  if (!document()->GetElementById("page-main"))
    return; // Standalone test/demo documents have their own controls.
  if (auto *Title = document()->GetElementById("title"))
    Title->SetInnerRML(escapeRml(resolveLabel(Definition.Title)));
  auto Build = [&](const char *Id,
                   const std::vector<services::uiconfig::MenuEntry> &Entries) {
    auto *Root = document()->GetElementById(Id);
    if (!Root)
      return;
    std::string Markup;
    for (const auto &Entry : Entries) {
      Markup += "<button id=\"" + escapeRmlAttribute(Entry.Id) +
                "\" data-action=\"" + escapeRmlAttribute(Entry.Action) +
                "\" data-label=\"" + escapeRmlAttribute(Entry.Label) +
                "\" data-condition=\"" + escapeRmlAttribute(Entry.Condition) +
                "\">" + escapeRml(resolveLabel(Entry.Label)) + "</button>";
    }
    Root->SetInnerRML(Markup);
  };
  Build("page-main", Definition.Main);
  Build("page-pause", Definition.Pause);
  setMultiplayer(Multiplayer);
}

void MainMenuPage::setMultiplayer(bool Value) {
  Multiplayer = Value;
  if (!document())
    return;
  for (const auto *Entries : {&Definition.Main, &Definition.Pause}) {
    for (const auto &Entry : *Entries) {
      if (auto *Node = document()->GetElementById(Entry.Id)) {
        const bool Visible =
            Entry.Condition == "always" ||
            (Entry.Condition == "multiplayer" && Multiplayer) ||
            (Entry.Condition == "singleplayer" && !Multiplayer);
        const auto Fact = EntryVisible.find(Entry.Action);
        const bool Allowed =
            Visible && (Fact == EntryVisible.end() || Fact->second);
        Node->SetAttribute("data-available", Allowed ? "yes" : "no");
        Node->SetProperty("display", Allowed ? "block" : "none");
      }
    }
  }
}

void MainMenuPage::setPlayerCard(const char *playerName, const char *modelPath,
                                 const char *modelLabel) {
  if (!document())
    return;
  using framework::TextUtil::escapeRml;
  if (auto *name = document()->GetElementById("player-name"))
    name->SetInnerRML(escapeRml(playerName ? playerName : ""));
  if (auto *label = document()->GetElementById("player-model-name"))
    label->SetInnerRML(escapeRml(modelLabel ? modelLabel : ""));
  if (auto *preview = document()->GetElementById("player-model")) {
    const Rml::String value = modelPath ? modelPath : "";
    if (preview->GetAttribute<Rml::String>("src", "") != value)
      preview->SetAttribute("src", value);
    preview->SetProperty("display", value.empty() ? "none" : "block");
  }
}

void MainMenuPage::setFooterNote(const char *text) {
  if (!document())
    return;
  if (auto *note = document()->GetElementById("menu-version")) {
    const Rml::String value = text ? text : "";
    if (note->GetInnerRML() != value)
      note->SetInnerRML(framework::TextUtil::escapeRml(value));
  }
}

void MainMenuPage::setFriends(
    const std::vector<services::friends::FriendEntry> &rows,
    bool steamAvailable) {
  if (!document())
    return;
  using framework::TextUtil::escapeRml;
  using framework::TextUtil::escapeRmlAttribute;
  Rml::Element *list = document()->GetElementById("friends-list");
  if (!list)
    return;
  if (rows.empty()) {
    list->SetInnerRML("");
    Rml::Element *empty = document()->GetElementById("friends-list-empty");
    if (!empty) {
      Rml::ElementPtr node = document()->CreateElement("div");
      if (!node)
        return;
      empty = node.get();
      empty->SetId("friends-list-empty");
      empty->SetClass("abc-muted", true);
      empty->SetProperty("display", "block");
      empty->SetProperty("height", "96dp");
      empty->SetProperty("line-height", "96dp");
      empty->SetProperty("text-align", "center");
      list->AppendChild(std::move(node));
    }
    empty->SetInnerRML(
        escapeRml(localized(steamAvailable ? "#MainMenu_FriendsEmpty"
                                           : "#MainMenu_FriendsUnavailable")));
    return;
  }
  std::string markup;
  for (std::size_t index = 0; index < rows.size(); index++) {
    const auto &row = rows[index];
    // Row 0 is the local player by the model's contract.
    const bool self = index == 0;
    markup += "<div class=\"friend-row";
    if (self)
      markup += " friend-self";
    else if (!row.Connect.empty())
      markup += " friend-playing";
    markup += "\" data-steamid=\"" + std::to_string(row.SteamId) +
              "\" data-connect=\"" + escapeRmlAttribute(row.Connect) + "\">";
    if (row.SteamId != 0)
      markup += "<img class=\"friend-avatar\" src=\"avatar://" +
                std::to_string(row.SteamId) + "\"/>";
    markup += "<span class=\"friend-name\">" + escapeRml(row.Name) + "</span>";
    // A game that is not this one is named as such: the board never
    // prints a raw AppID. Joinable friends in this game carry the game's
    // name; in-game friends without an address are just "in game".
    const char *state =
        self ? "#MainMenu_You"
             : (row.OtherGame
                    ? "#MainMenu_FriendOtherGame"
                    : (row.Connect.empty()
                           ? (row.InGame ? "#MainMenu_FriendInGame" : "")
                           : row.Game.c_str()));
    if (state && *state)
      markup += "<span class=\"friend-state\">" + escapeRml(localized(state)) +
                "</span>";
    markup += "</div>";
  }
  list->SetInnerRML(markup);
}

void MainMenuPage::showSection(Section section, bool focusDefault) {
  VisibleSection = section;
  if (document()) {
    for (const char *Id : {"friends-panel", "player-card"})
      if (auto *Node = document()->GetElementById(Id))
        Node->SetProperty("display",
                          section == Section::kMain ? "block" : "none");
    if (section != Section::kMain)
      closeFriendMenu();
  }
  if (Rml::Element *main =
          document() ? document()->GetElementById("page-main") : nullptr)
    main->SetProperty("display", section == Section::kMain ? "block" : "none");
  if (Rml::Element *pause =
          document() ? document()->GetElementById("page-pause") : nullptr)
    pause->SetProperty("display",
                       section == Section::kPause ? "block" : "none");
  if (!focusDefault)
    return;
  // Default focus: the section's first visible entry. Hidden entries
  // (singleplayer hides the browser/create/list row) are skipped through
  // the fact table, not the DOM - an element property only takes effect at
  // the next layout pass, while the sync needs the answer now.
  if (Rml::Element *node = sectionNode(section)) {
    const int count = node->GetNumChildren();
    for (int i = 0; i < count; i++) {
      Rml::Element *child = node->GetChild(i);
      if (!child || child->GetTagName() != "button" ||
          !child->HasAttribute("data-action"))
        continue;
      if (child->GetAttribute<Rml::String>("data-available", "yes") == "no")
        continue;
      const auto fact = EntryVisible.find(
          child->GetAttribute<Rml::String>("data-action", ""));
      if (fact != EntryVisible.end() && !fact->second)
        continue;
      child->Focus();
      return;
    }
  }
}

void MainMenuPage::setEntryVisible(const char *action, bool visible) {
  if (!action)
    return;
  EntryVisible[action] = visible; // the fact, readable back immediately
  Rml::Element *root = document();
  if (!root)
    return;
  // Both sections can carry the same action (settings/quit); every match
  // follows the same fact.
  const int count = root->GetNumChildren();
  for (int i = 0; i < count; i++)
    setEntryVisibleRecursive(root->GetChild(i), action, visible);
  setMultiplayer(Multiplayer);
}

void MainMenuPage::handle(Rml::Event &event) {
  const Rml::EventId id = event.GetId();
  if (id == Rml::EventId::Mousedown) {
    // Right button (RmlUi index 1) over a friend row opens the join menu;
    // every other button keeps its normal Rml handling.
    if (event.GetParameter<int>("button", 0) == 1)
      handleContextMenu(event);
    return;
  }
  if (id == Rml::EventId::Focus) {
    updateFocusStatus(event.GetCurrentElement());
    return;
  }
  if (id == Rml::EventId::Blur) {
    updateFocusStatus(nullptr);
    return;
  }
  // Any left click closes an open friend menu first: the menu itself is the
  // only click target that keeps it alive.
  Rml::Element *target = event.GetTargetElement();
  Rml::Element *menu =
      document() ? document()->GetElementById("friend-menu") : nullptr;
  bool insideMenu = false;
  for (Rml::Element *it = target; it && !insideMenu; it = it->GetParentNode())
    insideMenu = it == menu;
  if (!insideMenu)
    closeFriendMenu();
  else if (target && target->GetId() == "friend-menu-join") {
    if (JoinSink && !MenuFriendId.empty())
      JoinSink(strtoull(MenuFriendId.c_str(), nullptr, 10),
               MenuFriendAddress.c_str());
    closeFriendMenu();
    return;
  }

  // Click (delegated on the document): resolve the action entry, then hand
  // the fixed action name to the sink. A sink can replace itself or destroy
  // this page, so the callable is snapshotted and nothing is touched on the
  // page afterwards.
  Rml::Element *entry = actionNode(event.GetTargetElement());
  if (!entry)
    return;
  if (entry->GetAttribute<Rml::String>("data-available", "yes") == "no")
    return;
  const Rml::String action =
      entry->GetAttribute<Rml::String>("data-action", "");
  if (action.empty())
    return;
  const ActionSink Action = Sink;
  if (Action)
    Action(action.c_str());
}

// Right-click on a friend row opens the join menu at the pointer.
bool MainMenuPage::handleContextMenu(Rml::Event &event) {
  Rml::Element *row = event.GetTargetElement();
  Rml::Element *friendRow = nullptr;
  for (Rml::Element *it = row; it; it = it->GetParentNode()) {
    if (it->HasAttribute("data-steamid")) {
      friendRow = it;
      break;
    }
  }
  if (!friendRow)
    return false;
  const std::string connect =
      friendRow->GetAttribute<Rml::String>("data-connect", "").c_str();
  // A friend who is not in a joinable game has nothing to open a menu for;
  // the row still answers the click so the menu never lingers.
  closeFriendMenu();
  if (connect.empty())
    return false;
  openFriendMenu(
      friendRow->GetAttribute<Rml::String>("data-steamid", "").c_str(),
      connect.c_str(), event.GetParameter<int>("mouse_x", 0),
      event.GetParameter<int>("mouse_y", 0));
  if (document())
    if (Rml::Element *entry = document()->GetElementById("friend-menu-join"))
      entry->Focus();
  return true;
}

void MainMenuPage::openFriendMenu(const char *steamId, const char *address,
                                  int x, int y) {
  Rml::Element *menu =
      document() ? document()->GetElementById("friend-menu") : nullptr;
  if (!menu)
    return;
  MenuFriendId = steamId ? steamId : "";
  MenuFriendAddress = address ? address : "";
  char left[32];
  char top[32];
  // Keep the menu inside the window: it is 116dp wide and 24dp tall.
  const int clampedX = x > 640 ? x - 116 : x;
  _snprintf_s(left, sizeof(left), _TRUNCATE, "%dpx", clampedX);
  _snprintf_s(top, sizeof(top), _TRUNCATE, "%dpx", y);
  menu->SetProperty("left", left);
  menu->SetProperty("top", top);
  menu->SetProperty("display", "block");
}

void MainMenuPage::closeFriendMenu() {
  MenuFriendId.clear();
  MenuFriendAddress.clear();
  if (Rml::Element *menu =
          document() ? document()->GetElementById("friend-menu") : nullptr)
    menu->SetProperty("display", "none");
}

#ifndef NDEBUG
void MainMenuPage::debugOpenFriendMenu(Rml::Element *row, int x, int y) {
  if (!row)
    return;
  const std::string connect =
      row->GetAttribute<Rml::String>("data-connect", "").c_str();
  openFriendMenu(row->GetAttribute<Rml::String>("data-steamid", "").c_str(),
                 connect.c_str(), x, y);
}

void MainMenuPage::debugJoinFriend(const char *steamId) {
  if (JoinSink)
    JoinSink(strtoull(steamId ? steamId : "0", nullptr, 10),
             MenuFriendAddress.c_str());
}
#endif

void MainMenuPage::updateFocusStatus(Rml::Element *focused) {
  Rml::Element *status =
      document() ? document()->GetElementById("focus-status") : nullptr;
  if (!status)
    return;
  std::string label;
  if (focused && focused->HasAttribute("data-label"))
    label = resolveLabel(focused->GetAttribute<Rml::String>("data-label", ""));
  else
    label = resolveLabel("#MainMenu_FocusNone");
  status->SetInnerRML(framework::TextUtil::escapeRml(label));
}

void MainMenuPage::attachFocusBindings(Rml::Element *node) {
  if (!node)
    return;
  if (node->HasAttribute("data-label")) {
    addListener(node, "focus", &Listener);
    addListener(node, "blur", &Listener);
  }
  const int count = node->GetNumChildren();
  for (int i = 0; i < count; i++)
    attachFocusBindings(node->GetChild(i));
}

void MainMenuPage::setEntryVisibleRecursive(Rml::Element *node,
                                            const char *action, bool visible) {
  if (!node)
    return;
  if (node->HasAttribute("data-action") &&
      node->GetAttribute<Rml::String>("data-action", "") == action)
    node->SetProperty("display", visible ? "block" : "none");
  const int count = node->GetNumChildren();
  for (int i = 0; i < count; i++)
    setEntryVisibleRecursive(node->GetChild(i), action, visible);
}

Rml::Element *MainMenuPage::sectionNode(Section section) {
  Rml::ElementDocument *doc = document();
  if (!doc)
    return nullptr;
  return doc->GetElementById(section == Section::kMain ? "page-main"
                                                       : "page-pause");
}

void MainMenuPage::PageListener::ProcessEvent(Rml::Event &event) {
  Owner->handle(event);
}

} // namespace panels::mainmenu
