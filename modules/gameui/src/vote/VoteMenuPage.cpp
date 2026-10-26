// The vote creation page (moved from panels::votemenu, the first migrated
// page). The page is a module-owned interactive document: it presents itself
// from menu.state while the vote page is on top, projects the framework's
// VoteMenuModel through the sdk votemenu section, and navigates back through
// the menu section.
//
// The model, the roster seam (playerres), the server MapList feed and the
// `votemenu` command stay in the framework; this page owns only the DOM,
// the local tab/filter/selection gestures and the server-command send
// (console->serverCommand, the wire path ABCEnchance used).
#include "../shared/PageShared.h"

#include <cstdio>
#include <format>
#include <cstring>
#include <string>
#include <string_view>

namespace votepage {
namespace {

AbcUiDocument *Page = nullptr;
AbcUiElement *BoundRoot = nullptr;
uint64_t ClickBinding = 0;
AbcUiSubscription StateSubscription = 0;
AbcUiSubscription PaintSubscription = 0;
// Model revision of the last projection; ~0 forces the next render.
uint32_t RenderedRevision = ~0u;

constexpr const char *kDocumentName = "menuvotemenu";
constexpr const char *kDocumentPath =
    "mod://abcui.gameui/gameui/rml/votemenu.rml";

constexpr const char *kActionKind = "vm-kind";
constexpr const char *kActionFilter = "vm-filter";
constexpr const char *kActionSelect = "vm-select";
constexpr const char *kActionSubmit = "vm-submit";
constexpr const char *kActionCancel = "vm-cancel";

const char *kindId(int kind) {
  switch (kind) {
  case ABCUI_VOTE_KIND_BAN:
    return "ban";
  case ABCUI_VOTE_KIND_KICK:
    return "kick";
  case ABCUI_VOTE_KIND_KILL:
    return "kill";
  case ABCUI_VOTE_KIND_MAP:
    return "map";
  }
  return "ban";
}

bool parseKind(const std::string &text, int &out) {
  if (text == "ban")
    out = ABCUI_VOTE_KIND_BAN;
  else if (text == "kick")
    out = ABCUI_VOTE_KIND_KICK;
  else if (text == "kill")
    out = ABCUI_VOTE_KIND_KILL;
  else if (text == "map")
    out = ABCUI_VOTE_KIND_MAP;
  else
    return false;
  return true;
}

const char *startToken(int kind) {
  switch (kind) {
  case ABCUI_VOTE_KIND_BAN:
    return "#abcui.gameui/VoteMenu_StartBan";
  case ABCUI_VOTE_KIND_KICK:
    return "#abcui.gameui/VoteMenu_StartKick";
  case ABCUI_VOTE_KIND_KILL:
    return "#abcui.gameui/VoteMenu_StartKill";
  case ABCUI_VOTE_KIND_MAP:
    return "#abcui.gameui/VoteMenu_StartMap";
  }
  return "#abcui.gameui/VoteMenu_Start";
}

const char *filterToken(int kind) {
  return kind == ABCUI_VOTE_KIND_MAP
             ? "#abcui.gameui/VoteMenu_FilterMapHint"
             : "#abcui.gameui/VoteMenu_FilterPlayerHint";
}

const char *emptyToken(int kind) {
  return kind == ABCUI_VOTE_KIND_MAP ? "#abcui.gameui/VoteMenu_NoMaps"
                                     : "#abcui.gameui/VoteMenu_NoPlayers";
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

void showHint(const char *text) {
  const std::string line = resolve(text);
  setInnerRml("vm-hint", escapeText(line.c_str()));
  setDisplay("vm-hint", !line.empty());
}

// One row, exactly the old page's markup so the stylesheet is unchanged.
void appendRow(std::string &markup, int kind, int index,
               const AbcUiVoteCandidate &row) {
  markup += "<div class=\"vm-row";
  if (row.selected)
    markup += " vm-selected";
  markup += "\" data-action=\"vm-select\" data-index=\"";
  markup += std::to_string(index);
  markup += "\">";
  if (kind == ABCUI_VOTE_KIND_MAP) {
    markup += "<span class=\"vm-map-name\">";
    markup += escapeText(row.name);
    markup += "</span>";
  } else {
    markup += "<span class=\"vm-slot\">";
    markup += std::format("{:02d}", row.slot);
    markup += "</span><span class=\"vm-name\">";
    markup += escapeText(row.name);
    markup += "</span><span class=\"vm-real\">";
    markup += escapeText(row.realName[0] ? row.realName : "-");
    markup += "</span><span class=\"vm-id\">";
    markup += escapeText(row.steamId[0] ? row.steamId : "-");
    markup += "</span>";
  }
  markup += "</div>";
}

bool fetchRow(int index, AbcUiVoteCandidate &row) {
  row = AbcUiVoteCandidate{};
  row.structSize = sizeof(row);
  return Api->votemenu->visibleAt(index, &row) != 0;
}

void render() {
  if (!Page || !Api->documents->visible(Page) || !Api->votemenu)
    return;
  const uint32_t revision = Api->votemenu->revision();
  if (revision == RenderedRevision)
    return;
  RenderedRevision = revision;

  const int kind = Api->votemenu->kind();
  if (AbcUiElement *panel = node("vm-panel"))
    Api->ui->elementSetAttribute(panel, "data-kind", kindId(kind));

  for (int tab = ABCUI_VOTE_KIND_BAN; tab <= ABCUI_VOTE_KIND_MAP; ++tab) {
    const std::string id = std::string("vm-tab-") + kindId(tab);
    AbcUiElement *element = node(id.c_str());
    if (!element)
      continue;
    const bool active = kind == tab;
    Api->ui->elementSetClass(element, "vm-active", active ? 1 : 0);
    Api->ui->elementSetAttribute(element, "aria-pressed",
                                 active ? "true" : "false");
  }

  if (AbcUiElement *filter = node("vm-filter"))
    Api->ui->elementSetAttribute(filter, "placeholder",
                                 resolve(filterToken(kind)).c_str());
  setDisplay("vm-list-head", kind != ABCUI_VOTE_KIND_MAP);

  const int count = Api->votemenu->visibleCount();
  const int selectedIndex = Api->votemenu->selectedIndex();

  std::string markup;
  if (count == 0) {
    markup += "<div class=\"vm-empty\">";
    markup += escapeText(resolve(emptyToken(kind)));
    markup += "</div>";
  } else {
    for (int index = 0; index < count; ++index) {
      AbcUiVoteCandidate row;
      if (fetchRow(index, row))
        appendRow(markup, kind, index, row);
    }
  }
  setInnerRml("vm-list", markup);

  setInnerRml("vm-count", std::format("{}", count));

  std::string selectedName;
  AbcUiVoteCandidate selected;
  if (selectedIndex >= 0 && fetchRow(selectedIndex, selected))
    selectedName = selected.name;
  if (selectedName.empty())
    selectedName = resolve("#abcui.gameui/VoteMenu_NoneSelected");
  setInnerRml("vm-selected", escapeText(selectedName.c_str()));

  if (AbcUiElement *submit = node("vm-submit")) {
    Api->ui->elementSetInnerRML(
        submit, escapeText(resolve(startToken(kind)).c_str()).c_str());
    Api->ui->elementSetAttribute(submit, "disabled",
                                 selectedIndex >= 0 ? nullptr : "disabled");
  }
}

int32_t onClick(void *, AbcUiElement *target, const AbcUiElementEvent *event) {
  if (!event || !target || !event->type ||
      std::string_view(event->type) != "click")
    return 0;
  // Delegated row/tab lookup: the click lands on inner text as often as on
  // the element carrying the action.
  AbcUiElement *entry = target;
  std::string action;
  while (entry) {
    action = attributeOf(entry, "data-action");
    if (!action.empty())
      break;
    entry = Api->ui->elementParent(entry);
  }
  if (!entry || action.empty() || !Api->votemenu)
    return 0;

  if (action == kActionKind) {
    int parsed = ABCUI_VOTE_KIND_BAN;
    if (!parseKind(attributeOf(entry, "data-kind"), parsed))
      return 1;
    Api->votemenu->setKind(parsed);
    // Old behaviour: switching tabs clears the visible filter field (the
    // model's own filter clears inside setKind).
    if (AbcUiElement *filter = node("vm-filter"))
      Api->ui->elementSetValue(filter, "");
    showHint(nullptr);
    render();
    return 1;
  }
  if (action == kActionFilter) {
    std::string query;
    if (AbcUiElement *filter = node("vm-filter")) {
      query = moduleui::readString(Api->ui->elementValue, filter);
    }
    Api->votemenu->setFilter(query.c_str());
    showHint(nullptr);
    render();
    return 1;
  }
  if (action == kActionSelect) {
    const int index = Api->ui->elementAttributeInt(entry, "data-index", -1);
    if (index < 0)
      return 1;
    Api->votemenu->select(index);
    showHint(nullptr);
    render();
    return 1;
  }
  if (action == kActionSubmit) {
    if (Api->votemenu->selectedIndex() < 0) {
      showHint("#abcui.gameui/VoteMenu_NoSelection");
      return 1;
    }
    char command[256];
    char error[128];
    command[0] = '\0';
    error[0] = '\0';
    if (!Api->votemenu->submit(command, sizeof(command), error,
                               sizeof(error))) {
      render(); // the revalidation may have dropped the selection
      showHint(error[0] ? error : "#abcui.gameui/VoteMenu_NoSelection");
      return 1;
    }
    // ABCEnchance sent these verbs through pfnServerCmd; keep that wire path.
    Api->console->serverCommand(command);
    Api->menu->back();
    return 1;
  }
  if (action == kActionCancel) {
    Api->menu->back();
    return 1;
  }
  return 1; // an action-carrying element always consumes the click
}

void present() {
  if (!Page || !Api->votemenu)
    return;
  if (Api->documents->visible(Page))
    return;
  // Fresh opening: Ban page, empty filter, no selection (the old flow did
  // this before pushing the page).
  Api->votemenu->prepare();
  RenderedRevision = ~0u;
  Api->documents->show(Page);
  if (AbcUiElement *filter = node("vm-filter")) {
    // Fresh opening clears the visible filter field too (prepare cleared the
    // model side).
    Api->ui->elementSetValue(filter, "");
    Api->ui->elementFocus(filter);
  }
  showHint(nullptr);
  render();
}

void dismiss() {
  if (Page && Api->documents->visible(Page))
    Api->documents->hide(Page);
}

bool wantsPage(const AbcUiMenuState *state) {
  return state && state->up && state->page == ABCUI_MENU_PAGE_VOTE;
}

void onMenuState(void *, const void *payload) {
  const auto *state = static_cast<const AbcUiMenuState *>(payload);
  if (!state)
    return;
  if (wantsPage(state))
    present();
  else
    dismiss();
}

void onMenuPaint(void *, const void *payload) {
  const auto *state = static_cast<const AbcUiMenuState *>(payload);
  if (!state || !wantsPage(state) || !Page)
    return;
  if (!Api->documents->visible(Page))
    return;
  // The roster can change while the page is up; the model's change gate
  // bumps the revision and the render stays change-gated.
  Api->votemenu->refresh();
  render();
}

void onDocumentReady(void *) {
  RenderedRevision = ~0u;
  BoundRoot = Api->documents->root(Page);
  if (!BoundRoot)
    return;
  ClickBinding =
      Api->ui->elementAddListener(BoundRoot, "click", &onClick, nullptr);
}

void onDocumentHide(void *) {
  // A reopened page re-projects from scratch (the model may have changed
  // while the page was away).
  RenderedRevision = ~0u;
  showHint(nullptr);
}

void onDocumentClose(void *) {
  // The DOM is still alive: drop the listener before it goes (the framework
  // owns the listener object until it is removed).
  if (BoundRoot && ClickBinding) {
    Api->ui->elementRemoveListener(BoundRoot, "click", ClickBinding);
    ClickBinding = 0;
  }
  BoundRoot = nullptr;
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
  RenderedRevision = ~0u;
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

} // namespace votepage
