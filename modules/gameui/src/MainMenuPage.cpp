// The menu home page (moved from panels::mainmenu). ONE interactive document
// carrying the two home sections of the state machine: kMain (no-map) and
// kPause (in-game). Which section is visible is presentation of the
// controller's page stack - the page is told the section from menu.state and
// never infers game state from the DOM.
//
// The entry buttons, the character card, the footer note and the friends
// board project from the sdk menuconfig/social sections (the definition
// JSON and the engine facts stay framework-side); every entry carries a
// fixed action name and clicks resolve to the menu section's typed intents.
// Actions are intents only: nothing here hides the document or runs engine
// work - the sync owns every visibility change.
//
// Focus: tab-index rides the stylesheet (auto per entry); Enter/Space
// activation is RmlUi's focused-element click; the visible focus state is
// the data-label status line. The default focus per section is the
// section's first available entry, deferred while an overlay holds the
// screen (the state's overlays bitmask).
#include "PageShared.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace menupage {
namespace {

AbcUiDocument *Page = nullptr;
AbcUiElement *BoundRoot = nullptr;
uint64_t ClickBinding = 0;
uint64_t MousedownBinding = 0;
// Focus/blur do not bubble (RmlUi event spec): every data-label element
// carries its own pair so the status line follows whichever control holds
// focus.
std::vector<uint64_t> FocusBindings;
AbcUiSubscription StateSubscription = 0;
AbcUiSubscription PaintSubscription = 0;
// Model generation of the last friends projection (a steady list writes no
// DOM); ~0 forces the first render.
uint32_t RenderedRevision = ~0u;
bool Built = false;
bool MainVisible = true;

constexpr const char *kDocumentName = "mainmenu";
constexpr const char *kDocumentPath =
    "mod://abcui.gameui/gameui/rml/mainmenu.rml";

// data-label attributes carry "#token" form like text nodes do; resolve
// through the language table here (attributes skip Rml's translate hook).
std::string resolveLabel(const char *label) {
  if (!label || !*label)
    return {};
  if (*label == '#')
    return translate(label + 1);
  return label;
}

AbcUiElement *node(const char *id) {
  return Page ? Api->ui->querySelector(Page, id) : nullptr;
}

void setInnerRml(const char *id, const std::string &rml) {
  if (AbcUiElement *element = node(id))
    Api->ui->elementSetInnerRML(element, rml.c_str());
}

void setDisplay(const char *id, bool visible) {
  if (AbcUiElement *element = node(id))
    Api->ui->elementSetProperty(element, "display", visible ? "block" : "none");
}

void closeFriendMenu();
int32_t onFocusEvent(void *userdata, AbcUiElement *target,
                     const AbcUiElementEvent *event);

// The entry conditions key on the maxclients fact (the GameMenu.res
// "notsingle" split, verified against the shipped GameUI): the no-map home
// always shows its rows, the pause page's multiplayer rows follow the fact.
bool conditionAllows(const char *condition, bool multiplayer) {
  if (!condition || !*condition || std::strcmp(condition, "always") == 0)
    return true;
  if (std::strcmp(condition, "multiplayer") == 0)
    return multiplayer;
  if (std::strcmp(condition, "singleplayer") == 0)
    return !multiplayer;
  return true;
}

// One entry, exactly the old page's markup so the stylesheet is unchanged.
void buildEntries() {
  const char *title = Api->menuconfig->title();
  const std::string titleText =
      title && *title ? resolveLabel(title) : resolve("#MainMenu_Title");
  setInnerRml("title", escapeText(titleText.c_str()));

  for (const int32_t section :
       {ABCUI_MENU_SECTION_MAIN, ABCUI_MENU_SECTION_PAUSE}) {
    AbcUiElement *root =
        node(section == ABCUI_MENU_SECTION_MAIN ? "page-main" : "page-pause");
    if (!root)
      continue;
    std::string markup;
    const int32_t count = Api->menuconfig->entryCount(section);
    for (int32_t index = 0; index < count; ++index) {
      AbcUiMenuConfigEntry entry;
      entry.structSize = sizeof(entry);
      if (!Api->menuconfig->entryAt(section, index, &entry))
        continue;
      markup += "<button id=\"";
      markup += escapeAttribute(entry.id);
      markup += "\" data-action=\"";
      markup += escapeAttribute(entry.action);
      markup += "\" data-label=\"";
      markup += escapeAttribute(entry.label);
      markup += "\" data-condition=\"";
      markup += escapeAttribute(entry.condition);
      markup += "\">";
      markup += escapeText(resolveLabel(entry.label));
      markup += "</button>";
    }
    Api->ui->elementSetInnerRML(root, markup.c_str());
  }
}

// Focus/blur do not bubble; attach a pair on every data-label element.
void attachFocusBindings(AbcUiElement *nodeElement) {
  if (!nodeElement)
    return;
  if (!attributeOf(nodeElement, "data-label").empty()) {
    FocusBindings.push_back(Api->ui->elementAddListener(
        nodeElement, "focus", &onFocusEvent, nullptr));
    FocusBindings.push_back(Api->ui->elementAddListener(
        nodeElement, "blur", &onFocusEvent, nullptr));
  }
  const int32_t count = Api->ui->elementChildCount(nodeElement);
  for (int32_t index = 0; index < count; ++index)
    attachFocusBindings(Api->ui->elementChild(nodeElement, index));
}

// The status line follows whichever control holds focus (the data-label
// text); an unknown holder shows the generic note.
void updateFocusStatus(AbcUiElement *focused) {
  std::string label;
  if (focused) {
    const std::string raw = attributeOf(focused, "data-label");
    if (!raw.empty())
      label = resolveLabel(raw.c_str());
  }
  if (label.empty())
    label = resolve("#MainMenu_FocusNone");
  setInnerRml("focus-status", escapeText(label.c_str()));
}

// Makes one section the visible one; when requested, lands keyboard focus
// on the section's first available entry (safe while hidden: the focus
// lands after the next show).
void showSection(int32_t section, bool focusDefault) {
  MainVisible = section == ABCUI_MENU_SECTION_MAIN;
  setDisplay("friends-panel", MainVisible);
  setDisplay("player-card", MainVisible);
  setDisplay("page-main", MainVisible);
  setDisplay("page-pause", !MainVisible);
  if (!MainVisible)
    closeFriendMenu();
  if (!focusDefault)
    return;
  AbcUiElement *list = node(MainVisible ? "page-main" : "page-pause");
  if (!list)
    return;
  const int32_t count = Api->ui->elementChildCount(list);
  for (int32_t index = 0; index < count; ++index) {
    AbcUiElement *child = Api->ui->elementChild(list, index);
    if (!child || attributeOf(child, "data-action").empty())
      continue;
    if (attributeOf(child, "data-available") == "no")
      continue;
    Api->ui->elementFocus(child);
    return;
  }
}

// Entry availability follows the maxclients fact (re-read per beat).
void projectConditions() {
  const bool multiplayer = Api->menuconfig->multiplayer() != 0;
  for (const int32_t section :
       {ABCUI_MENU_SECTION_MAIN, ABCUI_MENU_SECTION_PAUSE}) {
    const int32_t count = Api->menuconfig->entryCount(section);
    for (int32_t index = 0; index < count; ++index) {
      AbcUiMenuConfigEntry entry;
      entry.structSize = sizeof(entry);
      if (!Api->menuconfig->entryAt(section, index, &entry))
        continue;
      if (AbcUiElement *element = node(entry.id)) {
        const bool allowed = conditionAllows(entry.condition, multiplayer);
        Api->ui->elementSetAttribute(element, "data-available",
                                     allowed ? "yes" : "no");
        Api->ui->elementSetProperty(element, "display",
                                    allowed ? "block" : "none");
      }
    }
  }
}

// Character card + footer facts: engine values the player edits in
// Settings; the page only projects them.
void projectFacts() {
  AbcUiPlayerCard card;
  card.structSize = sizeof(card);
  if (Api->menuconfig->playerCard(&card)) {
    setInnerRml("player-name", escapeText(card.playerName).c_str());
    setInnerRml("player-model-name", escapeText(card.modelLabel).c_str());
    if (AbcUiElement *preview = node("player-model")) {
      const std::string path = card.modelPath;
      if (attributeOf(preview, "src") != path)
        Api->ui->elementSetAttribute(preview, "src", path.c_str());
      Api->ui->elementSetProperty(preview, "display",
                                  path.empty() ? "none" : "block");
    }
  }
  char build[128];
  build[0] = '\0';
  Api->menuconfig->buildNote(build, sizeof(build));
  if (AbcUiElement *note = node("menu-version"))
    Api->ui->elementSetInnerRML(note, escapeText(build).c_str());
}

// Friends board projection: one row per entry, the local player first; the
// row's avatar resolves through avatar://<steamid> and the state line
// names the game - a friend in a game that is not this one is reported as
// such, never by AppID.
void projectFriends() {
  AbcUiElement *list = node("friends-list");
  if (!list)
    return;
  const int32_t count = Api->social->rowCount();
  if (count == 0) {
    // The shipped empty state: a centered muted note inside the list box.
    const bool available = Api->social->steamAvailable() != 0;
    std::string markup = "<div id=\"friends-list-empty\" class=\"abc-muted\">";
    markup += escapeText(resolve(available ? "#MainMenu_FriendsEmpty"
                                           : "#MainMenu_FriendsUnavailable"));
    markup += "</div>";
    Api->ui->elementSetInnerRML(list, markup.c_str());
    if (AbcUiElement *empty = node("friends-list-empty")) {
      Api->ui->elementSetProperty(empty, "display", "block");
      Api->ui->elementSetProperty(empty, "height", "96dp");
      Api->ui->elementSetProperty(empty, "line-height", "96dp");
      Api->ui->elementSetProperty(empty, "text-align", "center");
    }
    return;
  }
  std::string markup;
  for (int32_t index = 0; index < count; ++index) {
    AbcUiFriendRow row;
    row.structSize = sizeof(row);
    if (!Api->social->rowAt(index, &row))
      continue;
    markup += "<div class=\"friend-row";
    if (row.self)
      markup += " friend-self";
    else if (*row.connect)
      markup += " friend-playing";
    markup += "\" data-steamid=\"";
    markup += std::to_string(row.steamId);
    markup += "\" data-connect=\"";
    markup += escapeAttribute(row.connect);
    markup += "\">";
    if (row.steamId != 0) {
      markup += "<img class=\"friend-avatar\" src=\"avatar://";
      markup += std::to_string(row.steamId);
      markup += "\"/>";
    }
    markup += "<span class=\"friend-name\">";
    markup += escapeText(row.name);
    markup += "</span>";
    // Joinable friends in this game carry the game's name; in-game friends
    // without an address are just "in game".
    const char *state =
        row.self ? "#MainMenu_You"
                 : (row.otherGame
                        ? "#MainMenu_FriendOtherGame"
                        : (*row.connect
                               ? row.game
                               : (row.inGame ? "#MainMenu_FriendInGame" : "")));
    if (*state) {
      markup += "<span class=\"friend-state\">";
      markup += escapeText(resolve(state));
      markup += "</span>";
    }
    markup += "</div>";
  }
  Api->ui->elementSetInnerRML(list, markup.c_str());
}

void render() {
  if (!Page || !Api->documents->visible(Page) || !Api->menuconfig ||
      !Api->social)
    return;
  // The facts the player can edit mid-session re-project every beat; the
  // friends board alone is change-gated on the model's generation.
  const uint32_t generation = Api->social->generation();
  const bool generationMoved = generation != RenderedRevision;
  projectConditions();
  projectFacts();
  if (generationMoved) {
    RenderedRevision = generation;
    projectFriends();
  }
}

// The shell's fixed intents resolve through the menu section (the same
// typed controller events the built-in shell sent).
void dispatchAction(const std::string &action) {
  if (action == "resume")
    Api->menu->resume();
  else if (action == "disconnect")
    Api->menu->disconnect();
  else if (action == "quit")
    Api->menu->quit();
  else if (action == "settings")
    Api->menu->openPage(ABCUI_MENU_PAGE_SETTINGS);
  else if (action == "servers")
    Api->menu->openPage(ABCUI_MENU_PAGE_SERVERS);
  else if (action == "creategame")
    Api->menu->openPage(ABCUI_MENU_PAGE_CREATE_GAME);
  else if (action == "playerlist")
    Api->menu->openPage(ABCUI_MENU_PAGE_PLAYER_LIST);
  else if (action == "votemenu")
    Api->menu->openPage(ABCUI_MENU_PAGE_VOTE);
  else if (action == "console")
    Api->menu->toggleConsole();
  else if (action == "directconnect")
    Api->menu->directConnect();
  else
    Api->core->warn("shell action \"%s\" has no menu intent\n", action.c_str());
}

// The friend the open context menu belongs to (empty = closed).
std::string MenuFriendId;
std::string MenuFriendAddress;

void closeFriendMenu() {
  MenuFriendId.clear();
  MenuFriendAddress.clear();
  setDisplay("friend-menu", false);
}

void openFriendMenu(const char *steamId, const char *address, int x, int y) {
  AbcUiElement *menu = node("friend-menu");
  if (!menu)
    return;
  MenuFriendId = steamId ? steamId : "";
  MenuFriendAddress = address ? address : "";
  char left[32];
  char top[32];
  // Keep the menu inside the window: it is 116dp wide and 24dp tall.
  std::snprintf(left, sizeof(left), "%dpx", x > 640 ? x - 116 : x);
  std::snprintf(top, sizeof(top), "%dpx", y);
  Api->ui->elementSetProperty(menu, "left", left);
  Api->ui->elementSetProperty(menu, "top", top);
  Api->ui->elementSetProperty(menu, "display", "block");
}

// Right button (RmlUi index 1) over a friend row opens the join menu; a
// friend who is not in a joinable game has nothing to open a menu for.
bool handleContextMenu(AbcUiElement *target, float mouseX, float mouseY) {
  AbcUiElement *friendRow = target;
  std::string steamId;
  while (friendRow) {
    steamId = attributeOf(friendRow, "data-steamid");
    if (!steamId.empty())
      break;
    friendRow = Api->ui->elementParent(friendRow);
  }
  if (!friendRow)
    return false;
  const std::string connect = attributeOf(friendRow, "data-connect");
  closeFriendMenu();
  if (connect.empty())
    return false;
  openFriendMenu(steamId.c_str(), connect.c_str(), static_cast<int>(mouseX),
                 static_cast<int>(mouseY));
  if (AbcUiElement *entry = node("friend-menu-join"))
    Api->ui->elementFocus(entry);
  return true;
}

int32_t onMousedown(void *, AbcUiElement *target,
                    const AbcUiElementEvent *event) {
  if (!event || !target)
    return 0;
  if (std::strcmp(event->type, "mousedown") ||
      event->button != 1) // right button only
    return 0;
  handleContextMenu(target, event->mouseX, event->mouseY);
  return 0; // every other button keeps its normal Rml handling
}

int32_t onFocusEvent(void *, AbcUiElement *target,
                     const AbcUiElementEvent *event) {
  if (!event || !target)
    return 0;
  if (!std::strcmp(event->type, "focus"))
    updateFocusStatus(target);
  else if (!std::strcmp(event->type, "blur"))
    updateFocusStatus(nullptr);
  return 0;
}

int32_t onClick(void *, AbcUiElement *target, const AbcUiElementEvent *event) {
  if (!event || !target || std::strcmp(event->type, "click"))
    return 0;
  // Any left click closes an open friend menu first: the menu itself is
  // the only click target that keeps it alive.
  AbcUiElement *entry = target;
  bool insideMenu = false;
  AbcUiElement *menu = node("friend-menu");
  for (AbcUiElement *it = target; it && !insideMenu;
       it = Api->ui->elementParent(it))
    insideMenu = it == menu;
  if (!insideMenu) {
    closeFriendMenu();
  } else if (const char *id = Api->ui->elementId(target);
             id && !std::strcmp(id, "friend-menu-join")) {
    if (!MenuFriendId.empty())
      Api->social->join(std::strtoull(MenuFriendId.c_str(), nullptr, 10),
                        MenuFriendAddress.c_str());
    closeFriendMenu();
    return 1;
  }

  // Delegated entry lookup: the click lands on inner text as often as on
  // the element carrying the action. An unavailable entry consumes nothing
  // (the data-available fact).
  entry = target;
  std::string action;
  while (entry) {
    action = attributeOf(entry, "data-action");
    if (!action.empty())
      break;
    entry = Api->ui->elementParent(entry);
  }
  if (!entry || action.empty())
    return 0;
  if (attributeOf(entry, "data-available") == "no")
    return 1;
  dispatchAction(action);
  return 1;
}

void present(int32_t section, bool focusDefault) {
  if (!Page)
    return;
  Api->documents->show(Page);
  if (!Built) {
    // First presentation builds the entry buttons (the definition is the
    // framework's menuconfig section).
    buildEntries();
    if (BoundRoot)
      attachFocusBindings(BoundRoot);
    Built = true;
  }
  showSection(section, focusDefault);
  render();
}

void dismiss() {
  if (Page && Api->documents->visible(Page))
    Api->documents->hide(Page);
}

bool wantsPage(const AbcUiMenuState *state, int32_t &section) {
  if (!state || !state->up)
    return false;
  if (state->page == ABCUI_MENU_PAGE_MAIN) {
    section = ABCUI_MENU_SECTION_MAIN;
    return true;
  }
  if (state->page == ABCUI_MENU_PAGE_PAUSE) {
    section = ABCUI_MENU_SECTION_PAUSE;
    return true;
  }
  return false;
}

void onMenuState(void *, const void *payload) {
  const auto *state = static_cast<const AbcUiMenuState *>(payload);
  int32_t section = 0;
  if (wantsPage(state, section))
    present(section, state->overlays == 0);
  else
    dismiss();
}

void onMenuPaint(void *, const void *payload) {
  const auto *state = static_cast<const AbcUiMenuState *>(payload);
  int32_t section = 0;
  if (!state || !wantsPage(state, section) || !Page)
    return;
  if (!Api->documents->visible(Page))
    return;
  // The entry conditions, the card and the friends board follow their
  // facts while the page is up.
  render();
}

void onDocumentReady(void *) {
  BoundRoot = Api->documents->root(Page);
}

void onDocumentHide(void *) {
  RenderedRevision = ~0u;
  closeFriendMenu();
}

void onDocumentClose(void *) {
  // The DOM is still alive: drop the listeners before it goes (the
  // framework owns the listener objects until they are removed).
  if (BoundRoot && ClickBinding) {
    Api->ui->elementRemoveListener(BoundRoot, "click", ClickBinding);
    ClickBinding = 0;
  }
  if (BoundRoot && MousedownBinding) {
    Api->ui->elementRemoveListener(BoundRoot, "mousedown", MousedownBinding);
    MousedownBinding = 0;
  }
  for (uint64_t binding : FocusBindings) {
    if (binding && BoundRoot)
      Api->ui->elementRemoveListener(BoundRoot, "focus", binding);
  }
  for (uint64_t binding : FocusBindings) {
    if (binding && BoundRoot)
      Api->ui->elementRemoveListener(BoundRoot, "blur", binding);
  }
  FocusBindings.clear();
  BoundRoot = nullptr;
  Built = false;
  RenderedRevision = ~0u;
}

} // namespace

void load() {
  StateSubscription = Api->events->subscribe(ABCUI_CHANNEL_MENU_STATE, 0,
                                             &onMenuState, nullptr);
  PaintSubscription = Api->events->subscribe(ABCUI_CHANNEL_MENU_PAINT, 0,
                                             &onMenuPaint, nullptr);

  AbcUiDocumentDesc desc{};
  desc.structSize = sizeof(desc);
  desc.name = kDocumentName;
  desc.path = kDocumentPath;
  desc.flags = ABCUI_DOC_FLAG_MENU | ABCUI_DOC_FLAG_BACKDROP;
  desc.zIndex = 0;
  desc.userdata = nullptr;
  desc.onReady = &onDocumentReady;
  desc.onShow = nullptr;
  desc.onHide = &onDocumentHide;
  desc.onClose = &onDocumentClose;
  Page = Api->documents->create(&desc);
}

void shutdown() {
  // The document dies with the context; forget every borrowed handle.
  Page = nullptr;
  BoundRoot = nullptr;
  ClickBinding = 0;
  MousedownBinding = 0;
  FocusBindings.clear();
  Built = false;
  RenderedRevision = ~0u;
  MenuFriendId.clear();
  MenuFriendAddress.clear();
}

void unload() {
  if (StateSubscription) {
    Api->events->unsubscribe(StateSubscription);
    StateSubscription = 0;
  }
  if (PaintSubscription) {
    Api->events->unsubscribe(PaintSubscription);
    PaintSubscription = 0;
  }
  Page = nullptr;
}

} // namespace menupage
