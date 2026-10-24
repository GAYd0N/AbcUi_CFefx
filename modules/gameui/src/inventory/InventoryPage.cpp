// The inventory window page (moved from panels::inventory). The RmlUi
// replacement for the client's VGUI CInventoryPanel: the chained
// inventory/+inventory/-inventory commands suppress the native window (the
// framework keeps the command hooks), and this page projects the
// framework's item model - the InvAdd/InvRemove stream mirror - through the
// sdk inventory section.
//
// Layout: a dimmed scrim keeps the game readable but out of the way, a
// 520x360dp window carries the title bar, the item list on the left, the
// selected item's detail card on the right, and the action row at the
// bottom. No full-window menu backdrop: the world stays visible through
// the scrim. The keyboard policy (arrows/enter/delete, ESC, the bound
// key's close rule framework-side) answers the menu.commandkey veto feed;
// actions execute framework-side (the window closes, then the client
// command runs) and the read-through rule keeps the list truthful.
#include "../shared/PageShared.h"

#include <cstdio>
#include <format>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace inventorypage {
namespace {

AbcUiDocument *Page = nullptr;
AbcUiElement *BoundRoot = nullptr;
uint64_t ClickBinding = 0;
AbcUiSubscription StateSubscription = 0;
AbcUiSubscription PaintSubscription = 0;
AbcUiSubscription KeySubscription = 0;
// Model revision of the last projection; ~0 forces the next render.
uint32_t RenderedRevision = ~0u;

constexpr const char *kDocumentName = "menuinventory";
constexpr const char *kDocumentPath =
    "mod://abcui.gameui/gameui/rml/inventory.rml";

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

std::string localized(const char *token, const char *fallback) {
  std::string out = translate(token);
  return out.empty() ? std::string(fallback) : out;
}

// RmlUi's <button> is a plain element: it has no form-control disabled
// state, and a class rule loses the cascade to the variant classes. Apply
// the state as local properties and clear them again on the next state.
void setButtonState(const char *id, bool enabled, bool warning) {
  AbcUiElement *button = node(id);
  if (!button)
    return;
  if (enabled) {
    Api->ui->elementSetAttribute(button, "disabled", nullptr);
    Api->ui->elementSetProperty(button, "background-color", nullptr);
    Api->ui->elementSetProperty(button, "border", nullptr);
    Api->ui->elementSetProperty(button, "color", nullptr);
    if (warning) {
      Api->ui->elementSetProperty(button, "background-color", "transparent");
      Api->ui->elementSetProperty(button, "border", "1dp var(--abc-danger)");
      Api->ui->elementSetProperty(button, "color", "var(--abc-danger)");
    }
  } else {
    Api->ui->elementSetAttribute(button, "disabled", "disabled");
    Api->ui->elementSetProperty(button, "background-color", "var(--abc-inset)");
    Api->ui->elementSetProperty(button, "border", "1dp var(--abc-line-soft)");
    Api->ui->elementSetProperty(button, "color", "#4f5a62");
  }
  Api->ui->elementSetClass(button, "inv-disabled", enabled ? 0 : 1);
  Api->ui->elementSetClass(button, "inv-blocked", warning && enabled ? 1 : 0);
}

// The disabled attribute has no built-in click veto on a plain <button>,
// so the class marker is the veto (the click handler checks it).
bool buttonDisabled(const char *id) {
  AbcUiElement *button = node(id);
  return button && Api->ui->elementClassSet(button, "inv-disabled");
}

void render() {
  if (!Page || !Api->documents->visible(Page) || !Api->inventory)
    return;
  const uint32_t revision = Api->inventory->revision();
  if (revision == RenderedRevision)
    return;
  RenderedRevision = revision;

  const int32_t count = Api->inventory->itemCount();
  std::string markup;
  markup.reserve(4096);
  for (int32_t index = 0; index < count; ++index) {
    AbcUiInvItem item;
    item.structSize = sizeof(item);
    if (!Api->inventory->itemAt(index, &item))
      continue;
    markup += "<div class=\"inv-row";
    if (item.selected)
      markup += " inv-selected";
    markup += "\" data-serial=\"";
    markup += std::to_string(item.serial);
    markup += "\"><span class=\"inv-row-index\">";
    markup += std::format("{:02d}", index + 1);
    markup += "</span><span class=\"inv-row-name\">";
    markup += escapeText(
        item.name[0] ? item.name
                     : localized("Invmenu_NoItemName", "Nameless item"));
    markup += "</span><span class=\"inv-row-mark\">\xe2\x80\xba</span></div>";
  }

  if (AbcUiElement *list = node("inv-list")) {
    Api->ui->elementSetInnerRML(list, markup.c_str());
    Api->ui->elementSetProperty(list, "display", count == 0 ? "none" : "block");
  }
  setDisplay("inv-empty", count == 0);
  setInnerRml("inv-count", std::to_string(count));

  // Detail card: with a selection the name/description and the server's
  // availability flags are projected; without one the card shrinks to the
  // "select an item" prompt. The hidden name/description keep the native
  // no-selection text (screen readers and the fallback projection).
  AbcUiInvItem selected{};
  bool hasSelection = false;
  for (int32_t index = 0; index < count; ++index) {
    AbcUiInvItem item;
    item.structSize = sizeof(item);
    if (Api->inventory->itemAt(index, &item) && item.selected) {
      selected = item;
      hasSelection = true;
      break;
    }
  }
  setDisplay("inv-detail-body", hasSelection);
  setDisplay("inv-tags", hasSelection);
  setDisplay("inv-select-hint", !hasSelection);

  const std::string noSelection =
      localized("Invmenu_NoItemSelected", "No item selected");
  setInnerRml("inv-name",
              escapeText(!hasSelection ? noSelection.c_str()
                                       : (selected.name[0]
                                              ? selected.name
                                              : localized("Invmenu_NoItemName",
                                                          "Nameless item")))
                  .c_str());
  setInnerRml("inv-desc",
              escapeText(!hasSelection
                             ? noSelection.c_str()
                             : (selected.description[0]
                                    ? selected.description
                                    : localized("Invmenu_NoItemDescription",
                                                "No item description")))
                  .c_str());

  if (hasSelection) {
    const auto setTag = [&](const char *id, bool ok, const char *okToken,
                            const char *blockedToken, const char *okFallback,
                            const char *blockedFallback) {
      setInnerRml(id, escapeText(localized(ok ? okToken : blockedToken,
                                           ok ? okFallback : blockedFallback))
                          .c_str());
      if (AbcUiElement *tag = node(id)) {
        Api->ui->elementSetClass(tag, "inv-tag-ok", ok ? 1 : 0);
        Api->ui->elementSetClass(tag, "inv-tag-blocked", ok ? 0 : 1);
      }
    };
    setTag("inv-tag-usable", selected.canActivate != 0, "Invmenu_Usable",
           "Invmenu_NotUsable", "Can use", "Cannot use");
    setTag("inv-tag-droppable", selected.canDrop != 0, "Invmenu_Droppable",
           "Invmenu_NotDroppable", "Can drop", "Cannot drop");
  }

  // Native UpdateSelection enables Activate/Drop only with a selection and
  // Drop ALL only with items; the can* flags are a warning tint, not a
  // disable (the native handler still sends the command and lets the
  // server decide).
  setButtonState("inv-activate", hasSelection,
                 hasSelection && !selected.canActivate);
  setButtonState("inv-drop", hasSelection, hasSelection && !selected.canDrop);
  setButtonState("inv-dropall", count != 0, false);
}

// The safe default focus: the selected row when one exists, else the list.
void refocusDefault() {
  AbcUiElement *list = node("inv-list");
  if (!list)
    return;
  const int32_t count = Api->inventory->itemCount();
  for (int32_t index = 0; index < count; ++index) {
    AbcUiInvItem item;
    item.structSize = sizeof(item);
    if (!Api->inventory->itemAt(index, &item) || !item.selected)
      continue;
    if (AbcUiElement *row = Api->ui->elementChild(list, index)) {
      Api->ui->elementFocus(row);
      return;
    }
  }
  Api->ui->elementFocus(list);
}

int32_t onKey(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiKeyEvent *>(payload);
  if (!event || !event->down || !Api->inventory || !Page ||
      !Api->documents->visible(Page))
    return 0;
  const int keynum = event->keynum;

  if (keynum == ABCUI_KEY_UPARROW || keynum == ABCUI_KEY_DOWNARROW) {
    // Consumed even at the list ends.
    if (Api->inventory->moveSelection(keynum == ABCUI_KEY_UPARROW ? -1 : 1)) {
      render();
      refocusDefault();
    }
    return 1;
  }
  if ((keynum == ABCUI_KEY_ENTER || keynum == ABCUI_KEY_KP_ENTER) &&
      Api->inventory->activate(ABCUI_INV_ACTION_ACTIVATE)) {
    return 1; // issued: the window closed framework-side
  }
  if (keynum == ABCUI_KEY_DELETE &&
      Api->inventory->activate(ABCUI_INV_ACTION_DROP)) {
    return 1;
  }
  if (keynum == ABCUI_KEY_ESCAPE) {
    // ESC is the single dismiss gesture.
    Api->inventory->dismiss();
    return 1;
  }
  return 0;
}

int32_t onClick(void *, AbcUiElement *target, const AbcUiElementEvent *event) {
  if (!event || !target || std::string_view(event->type) != "click")
    return 0;
  // Walk up from the event target to the element carrying the intent; the
  // rows carry data-serial, buttons carry data-action.
  AbcUiElement *entry = target;
  std::string action;
  while (entry) {
    action = attributeOf(entry, "data-action");
    if (!action.empty())
      break;
    entry = Api->ui->elementParent(entry);
  }
  if (entry && !action.empty()) {
    if (buttonDisabled("inv-activate") && action == "inv-activate")
      return 1;
    if (buttonDisabled("inv-drop") && action == "inv-drop")
      return 1;
    if (action == "inv-close")
      Api->inventory->dismiss();
    else if (action == "inv-activate")
      Api->inventory->activate(ABCUI_INV_ACTION_ACTIVATE);
    else if (action == "inv-drop")
      Api->inventory->activate(ABCUI_INV_ACTION_DROP);
    else if (action == "inv-dropall")
      Api->inventory->activate(ABCUI_INV_ACTION_DROP_ALL);
    return 1;
  }

  // A row click selects.
  entry = target;
  std::string serial;
  while (entry) {
    serial = attributeOf(entry, "data-serial");
    if (!serial.empty())
      break;
    entry = Api->ui->elementParent(entry);
  }
  if (entry && !serial.empty()) {
    Api->inventory->select(std::strtoull(serial.c_str(), nullptr, 10));
    render();
  }
  return 1;
}

void present() {
  if (!Page || !Api->inventory)
    return;
  if (Api->documents->visible(Page))
    return;
  // A fresh opening projects even when the model did not move while the
  // window was closed (selection is page-visible state); the focus lands
  // once the document is actually visible.
  RenderedRevision = ~0u;
  Api->documents->show(Page);
  render();
  refocusDefault();
}

void dismiss() {
  if (Page && Api->documents->visible(Page))
    Api->documents->hide(Page);
}

bool wantsPage(const AbcUiMenuState *state) {
  return state && state->up && state->page == ABCUI_MENU_PAGE_INVENTORY;
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
  // The server stream can move the list while the window is up; the render
  // stays change-gated on the model revision.
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
  // A reopened window re-projects from scratch.
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

} // namespace inventorypage
