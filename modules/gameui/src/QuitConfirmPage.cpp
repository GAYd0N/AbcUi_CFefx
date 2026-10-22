// The quit confirmation overlay (moved out of the framework's menushared
// panel). The controller owns the overlay state; this page presents its own
// document from the menu.state overlay bit and resolves it through the menu
// section. Cancel autofocuses, so a stray Enter never ends the game.
#include "PageShared.h"

#include <cstring>

namespace quitconfirmpage {
namespace {

constexpr const char *kDocumentName = "menuconfirm";
constexpr const char *kDocumentPath =
    "mod://abcui.gameui/gameui/rml/confirmquit.rml";

AbcUiDocument *Page = nullptr;
AbcUiElement *Root = nullptr;
uint64_t ClickBinding = 0;
AbcUiSubscription StateSubscription = 0;

bool wanted(const AbcUiMenuState *state) {
  return state && state->up &&
         (state->overlays & ABCUI_MENU_OVERLAY_CONFIRM_QUIT) != 0;
}

AbcUiElement *node(const char *id) {
  return Page ? Api->ui->querySelector(Page, id) : nullptr;
}

int32_t onClick(void *, AbcUiElement *target, const AbcUiElementEvent *event) {
  if (!event || !target || std::strcmp(event->type, "click") != 0)
    return 0;
  std::string action;
  for (AbcUiElement *entry = target; entry;
       entry = Api->ui->elementParent(entry)) {
    action = attributeOf(entry, "data-action");
    if (!action.empty())
      break;
  }
  if (action == "quit-yes") {
    Api->menu->quitResolve(1);
    return 1;
  }
  if (action == "quit-no") {
    Api->menu->quitResolve(0);
    return 1;
  }
  return 0;
}

void onDocumentReady(void *) {
  Root = Api->documents->root(Page);
  if (Root)
    ClickBinding =
        Api->ui->elementAddListener(Root, "click", &onClick, nullptr);
}

void onDocumentShow(void *) {
  // The document's autofocus attribute lands on #confirm-no; assert it again
  // for the first show after a context rebuild.
  if (AbcUiElement *cancel = node("confirm-no"))
    Api->ui->elementFocus(cancel);
}

void onDocumentClose(void *) {
  if (Root && ClickBinding)
    Api->ui->elementRemoveListener(Root, "click", ClickBinding);
  ClickBinding = 0;
  Root = nullptr;
}

void onMenuState(void *, const void *payload) {
  const auto *state = static_cast<const AbcUiMenuState *>(payload);
  if (!Page)
    return;
  const bool visible = Api->documents->visible(Page) != 0;
  if (wanted(state)) {
    if (!visible)
      Api->documents->show(Page);
  } else if (visible) {
    Api->documents->hide(Page);
  }
}

} // namespace

void load() {
  StateSubscription = Api->events->subscribe(ABCUI_CHANNEL_MENU_STATE, 0,
                                             &onMenuState, nullptr);

  AbcUiDocumentDesc desc{};
  desc.structSize = sizeof(desc);
  desc.name = kDocumentName;
  desc.path = kDocumentPath;
  desc.flags = ABCUI_DOC_FLAG_MENU;
  desc.zIndex = ABCUI_DOC_ZINDEX_QUIT_CONFIRM;
  desc.userdata = nullptr;
  desc.onReady = &onDocumentReady;
  desc.onShow = &onDocumentShow;
  desc.onHide = nullptr;
  desc.onClose = &onDocumentClose;
  Page = Api->documents->create(&desc);
}

void shutdown() {
  // The document dies with the context; forget every borrowed handle.
  Page = nullptr;
  Root = nullptr;
  ClickBinding = 0;
}

void unload() {
  if (StateSubscription) {
    Api->events->unsubscribe(StateSubscription);
    StateSubscription = 0;
  }
  Page = nullptr;
  Root = nullptr;
  ClickBinding = 0;
}

} // namespace quitconfirmpage
