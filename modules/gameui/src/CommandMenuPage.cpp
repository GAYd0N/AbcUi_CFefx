// The in-game command menu page (moved from panels::commandmenu). Projects
// the framework's commandmenu model through the sdk commandmenu section as
// a translucent, left-anchored stack of at most three columns, and answers
// the menu.commandkey veto feed with the navigation policy (arrows, enter,
// back, escape, one-based digit shortcuts). The definition JSON, its parser
// and the map filter stay in the framework; activations execute there too
// (a command closes the menu first and then runs the engine command), so
// this page owns only the DOM, the selection focus and the gestures.
//
// No full-window backdrop: the world remains visible behind the columns
// (the document carries the menu flag only).
#include "PageShared.h"

#include <cstring>
#include <string>

namespace commandmenupage {
namespace {

AbcUiDocument *Page = nullptr;
AbcUiElement *BoundRoot = nullptr;
uint64_t ClickBinding = 0;
AbcUiSubscription StateSubscription = 0;
AbcUiSubscription PaintSubscription = 0;
AbcUiSubscription KeySubscription = 0;
// Model revision of the last projection; ~0 forces the next render.
uint32_t RenderedRevision = ~0u;

constexpr const char *kDocumentName = "menucommandmenu";
constexpr const char *kDocumentPath =
    "mod://abcui.gameui/gameui/rml/commandmenu.rml";

constexpr const char *kActionActivate = "cm-activate";

// The HLSDK numpad block (NumLock-on digits), engine key numbers.
int shortcutOf(int keynum) {
  switch (keynum) {
  case 170:
    return 0; // K_KP_INS
  case 166:
    return 1; // K_KP_END
  case 167:
    return 2; // K_KP_DOWNARROW
  case 168:
    return 3; // K_KP_PGDN
  case 163:
    return 4; // K_KP_LEFTARROW
  case 164:
    return 5; // K_KP_5
  case 165:
    return 6; // K_KP_RIGHTARROW
  case 160:
    return 7; // K_KP_HOME
  case 161:
    return 8; // K_KP_UPARROW
  case 162:
    return 9; // K_KP_PGUP
  default:
    return -1;
  }
}

AbcUiElement *node(const char *id) {
  return Page ? Api->ui->querySelector(Page, id) : nullptr;
}

// Command-menu labels resolve literal text through the language table as
// well (exact source match) - this keeps the JSON readable while allowing
// the complete shipped menu to be localized; explicit "#token" labels
// remain available for custom entries.
std::string resolveLabel(const char *text) {
  if (!text || !*text)
    return {};
  if (*text == '#')
    return translate(text + 1);
  return translate(text);
}

// One column, exactly the old page's markup so the stylesheet is unchanged.
void appendColumn(std::string &markup, int32_t column, int32_t slot) {
  const int32_t count = Api->commandmenu->itemCountAt(column);
  const int32_t selected = Api->commandmenu->selectedAt(column);
  markup += "<div class=\"cm-column\" id=\"cm-column-";
  markup += std::to_string(slot);
  markup += "\">";
  for (int32_t index = 0; index < count; ++index) {
    AbcUiCommandMenuItem item;
    item.structSize = sizeof(item);
    if (!Api->commandmenu->itemAt(column, index, &item))
      continue;
    markup += "<div id=\"cm-row-";
    markup += std::to_string(column);
    markup += "-";
    markup += std::to_string(index);
    markup += "\" class=\"cm-row";
    if (index == selected)
      markup += " cm-selected";
    markup += item.hasItems ? " cm-branch" : " cm-leaf";
    markup += "\" data-action=\"";
    markup += kActionActivate;
    markup += "\" data-column=\"";
    markup += std::to_string(column);
    markup += "\" data-index=\"";
    markup += std::to_string(index);
    markup += "\">";
    markup += "<span class=\"cm-key\">";
    markup += std::to_string(index + 1);
    markup += "</span><span class=\"cm-label\">";
    markup += escapeText(resolveLabel(item.label));
    markup += "</span>";
    if (item.hasItems)
      markup += "<span class=\"cm-arrow\">&gt;</span>";
    markup += "</div>";
  }
  markup += "</div>";
}

void focusSelected() {
  const int32_t columns = Api->commandmenu->columnCount();
  if (columns == 0)
    return;
  const std::string id =
      "cm-row-" + std::to_string(columns - 1) + "-" +
      std::to_string(Api->commandmenu->selectedAt(columns - 1));
  if (AbcUiElement *row = node(id.c_str()))
    Api->ui->elementFocus(row);
}

void render() {
  if (!Page || !Api->documents->visible(Page) || !Api->commandmenu)
    return;
  const uint32_t revision = Api->commandmenu->revision();
  if (revision == RenderedRevision)
    return;
  RenderedRevision = revision;

  if (AbcUiElement *title = node("cm-title")) {
    const std::string text = resolveLabel(Api->commandmenu->title());
    const std::string shown =
        text.empty() ? resolve("#CommandMenu_Title") : escapeText(text);
    Api->ui->elementSetInnerRML(title, shown.c_str());
  }

  AbcUiElement *columns = node("cm-columns");
  if (!columns)
    return;

  const int32_t count = Api->commandmenu->visibleColumnCount();
  const int32_t first = Api->commandmenu->firstVisibleColumn();
  if (AbcUiElement *shell = node("cm-shell")) {
    const int32_t slots = count == 0 ? 1 : count;
    Api->ui->elementSetClass(shell, "cm-cols-2", slots == 2 ? 1 : 0);
    Api->ui->elementSetClass(shell, "cm-cols-3", slots == 3 ? 1 : 0);
  }

  std::string markup;
  markup.reserve(8192);
  if (count == 0) {
    markup += "<div class=\"cm-column\" id=\"cm-column-empty\">";
    markup += "<div class=\"cm-empty\">";
    markup += escapeText(resolve("#CommandMenu_Empty"));
    markup += "</div></div>";
  } else {
    for (int32_t column = first; column < first + count; ++column)
      appendColumn(markup, column, column - first);
  }
  Api->ui->elementSetInnerRML(columns, markup.c_str());
  focusSelected();
}

// A kCommand/kClose result already ran framework-side (the menu closed);
// only a submenu asks for a fresh projection here.
void handleResult(int32_t result) {
  switch (result) {
  case ABCUI_CMDMENU_RESULT_SUBMENU:
    render();
    break;
  case ABCUI_CMDMENU_RESULT_NONE:
  case ABCUI_CMDMENU_RESULT_COMMAND:
  case ABCUI_CMDMENU_RESULT_CLOSE:
  default:
    break;
  }
}

// The page's navigation policy over the claimed key edges (engine key
// numbers; printable ASCII carries letters/digits).
int32_t onKey(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiKeyEvent *>(payload);
  if (!event || !event->down || !Api->commandmenu || !Page ||
      !Api->documents->visible(Page))
    return 0;
  const int keynum = event->keynum;

  if (keynum == ABCUI_KEY_UPARROW) {
    if (Api->commandmenu->moveSelection(-1))
      render();
    return 1;
  }
  if (keynum == ABCUI_KEY_DOWNARROW) {
    if (Api->commandmenu->moveSelection(1))
      render();
    return 1;
  }
  if (keynum == ABCUI_KEY_LEFTARROW || keynum == ABCUI_KEY_BACKSPACE) {
    // LEFT/BACKSPACE walks one column back so deep menus can be explored
    // without closing; kClose at the root already dismissed.
    handleResult(Api->commandmenu->back());
    return 1;
  }
  if (keynum == ABCUI_KEY_RIGHTARROW) {
    handleResult(Api->commandmenu->openSelected());
    return 1;
  }
  if (keynum == ABCUI_KEY_ENTER || keynum == ABCUI_KEY_KP_ENTER) {
    handleResult(Api->commandmenu->activateSelected());
    return 1;
  }
  if (keynum == ABCUI_KEY_ESCAPE) {
    // ESC is the single full dismiss gesture.
    Api->commandmenu->dismiss();
    return 1;
  }
  if (keynum >= '0' && keynum <= '9') {
    handleResult(Api->commandmenu->activateShortcut(keynum - '0'));
    return 1;
  }
  const int shortcut = shortcutOf(keynum);
  if (shortcut >= 0) {
    handleResult(Api->commandmenu->activateShortcut(shortcut));
    return 1;
  }
  return 0;
}

int32_t onClick(void *, AbcUiElement *target, const AbcUiElementEvent *event) {
  if (!event || !target || !event->type || std::strcmp(event->type, "click"))
    return 0;
  // Delegated row lookup: the click lands on inner text as often as on the
  // element carrying the action.
  AbcUiElement *entry = target;
  std::string action;
  while (entry) {
    action = attributeOf(entry, "data-action");
    if (!action.empty())
      break;
    entry = Api->ui->elementParent(entry);
  }
  if (!entry || action != kActionActivate || !Api->commandmenu)
    return 0;
  const int column = Api->ui->elementAttributeInt(entry, "data-column", -1);
  const int index = Api->ui->elementAttributeInt(entry, "data-index", -1);
  if (column < 0 || index < 0)
    return 1;
  handleResult(Api->commandmenu->activateAt(column, index));
  return 1;
}

void present() {
  if (!Page || !Api->commandmenu)
    return;
  if (Api->documents->visible(Page)) {
    render();
    return;
  }
  // The flow's open() reset the model to the root; the fresh projection
  // takes the selected row's focus (a visible document is the focus
  // precondition).
  RenderedRevision = ~0u;
  Api->documents->show(Page);
  render();
}

void dismiss() {
  if (Page && Api->documents->visible(Page))
    Api->documents->hide(Page);
}

bool wantsPage(const AbcUiMenuState *state) {
  return state && state->up && state->page == ABCUI_MENU_PAGE_COMMAND;
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
  // Nothing framework-side moves the model while the page is up (the
  // gestures run through the section); the gate keeps this cheap.
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
  // A reopened page re-projects from scratch (the model resets on open).
  RenderedRevision = ~0u;
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
  KeySubscription = Api->events->subscribeVetoAggregate(
      ABCUI_CHANNEL_MENU_COMMAND_KEY, 0, &onKey, nullptr);

  AbcUiDocumentDesc desc{};
  desc.structSize = sizeof(desc);
  desc.name = kDocumentName;
  desc.path = kDocumentPath;
  desc.flags = ABCUI_DOC_FLAG_MENU;
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
  if (KeySubscription) {
    Api->events->unsubscribe(KeySubscription);
    KeySubscription = 0;
  }
  Page = nullptr;
}

} // namespace commandmenupage
