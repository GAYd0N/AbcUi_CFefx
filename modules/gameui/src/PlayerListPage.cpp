// The player-list page (moved from panels::playerlist). Projects the
// framework's PlayerListModel through the sdk playerlist section: the
// roster snapshot, the identity-keyed selection and the read-back mute
// toggle. The model, the roster seam (playerres) and the voice mute backend
// stay in the framework; this page owns only the DOM, the row selection and
// the fixed intents (select / mute-toggle / back through menu.section).
//
// Parity points carried over from the built-in page (see the model header
// for the shipped-dialog evidence): rows carry name plus a state suffix
// (Muted / CPU Player / Spectating), the mute label toggles and is disabled
// exactly when the shipped button was, and every player-derived string is
// escaped before it reaches the DOM.
#include "PageShared.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace playerlistpage {
namespace {

AbcUiDocument *Page = nullptr;
AbcUiElement *BoundRoot = nullptr;
uint64_t ClickBinding = 0;
AbcUiSubscription StateSubscription = 0;
AbcUiSubscription PaintSubscription = 0;
// Model revision of the last projection; ~0 forces the next render.
uint32_t RenderedRevision = ~0u;

constexpr const char *kDocumentName = "menuplayerlist";
constexpr const char *kDocumentPath =
    "mod://abcui.gameui/gameui/rml/playerlist.rml";

constexpr const char *kActionSelect = "pl-row";
constexpr const char *kActionMute = "pl-mute";
constexpr const char *kActionBack = "pl-close";

AbcUiElement *node(const char *id) {
  return Page ? Api->ui->querySelector(Page, id) : nullptr;
}

void setInnerRml(const char *id, const std::string &rml) {
  if (AbcUiElement *element = node(id))
    Api->ui->elementSetInnerRML(element, rml.c_str());
}

bool fetchRow(int index, AbcUiPlayerRow &row) {
  row = AbcUiPlayerRow{};
  row.structSize = sizeof(row);
  return Api->playerlist->rosterAt(index, &row) != 0;
}

// The selected row's slot (0 when nothing is selected) - the toggle log's
// identity, re-read so it cannot outlive the selection it reports.
int selectedSlot() {
  const int count = Api->playerlist->rosterCount();
  for (int index = 0; index < count; ++index) {
    AbcUiPlayerRow row;
    if (fetchRow(index, row) && row.selected)
      return row.slot;
  }
  return 0;
}

// One row, exactly the old page's markup so the stylesheet is unchanged.
// The identity rides in data-* attributes: a click is answered against the
// row it landed on, not against whatever the live snapshot holds then.
void appendRow(std::string &markup, const AbcUiPlayerRow &row) {
  markup += "<div class=\"pl-row";
  if (row.selected)
    markup += " pl-selected";
  markup += "\" data-action=\"pl-row\" data-index=\"";
  markup += std::to_string(row.slot);
  markup += "\"";
  if (row.steamId64 != 0) {
    char id[32];
    std::snprintf(id, sizeof(id), "%llu",
                  static_cast<unsigned long long>(row.steamId64));
    markup += " data-steamid=\"";
    markup += id;
    markup += "\"";
  }
  markup += " data-name=\"";
  markup += escapeAttribute(row.name);
  markup += "\">";
  markup += "<span class=\"pl-name\">";
  markup += escapeText(row.name);
  markup += "</span>";

  std::string state;
  if (row.flags & ABCUI_PL_FLAG_LOCAL)
    state = resolve("#PL_You");
  else if (row.flags & ABCUI_PL_FLAG_BOT)
    state = resolve("#PL_Bot");
  if (row.flags & ABCUI_PL_FLAG_MUTED) {
    if (!state.empty())
      state += " ";
    state += resolve("#PL_MutedLabel");
  } else if (row.flags & ABCUI_PL_FLAG_SPECTATING) {
    if (!state.empty())
      state += " ";
    state += resolve("#PL_Spectating");
  }
  if (!state.empty()) {
    markup += "<span class=\"pl-state\">";
    markup += escapeText(state);
    markup += "</span>";
  }
  markup += "</div>";
}

void render() {
  if (!Page || !Api->documents->visible(Page) || !Api->playerlist)
    return;
  const uint32_t revision = Api->playerlist->revision();
  if (revision == RenderedRevision)
    return;
  RenderedRevision = revision;

  const int count = Api->playerlist->rosterCount();
  std::string markup;
  int rows = 0;
  // The mute control keys on the selected row's flags; the projection walk
  // remembers them instead of a second selection read (one truth).
  AbcUiPlayerRow selected{};
  for (int index = 0; index < count; ++index) {
    AbcUiPlayerRow row;
    if (!fetchRow(index, row))
      continue;
    if (row.selected)
      selected = row;
    appendRow(markup, row);
    ++rows;
  }
  // The shipped empty-state label lives on the ListPanel; the same here
  // when the roster holds only the local player (the shipped dialog listed
  // every non-empty name including your own row).
  if (rows == 0)
    markup = "<p id=\"pl-empty\">" + escapeText(resolve("#PL_Empty")) + "</p>";
  setInnerRml("pl-list", markup);

  // Mute control: label follows the selected player's ACTUAL backend state
  // (a read-through, never an optimistic flip); enabled only when the model
  // can actually act (backend present, mutable selection) - the shipped
  // enable rule. Bots, the local player and SteamID-less rows are not
  // mutable (the model refuses them too; the predicate keeps the button
  // honest).
  if (AbcUiElement *mute = node("pl-mute")) {
    const bool available = Api->playerlist->muteAvailable() != 0;
    const bool mutableSelection =
        selected.structSize != 0 && selected.steamId64 != 0 &&
        !(selected.flags & ABCUI_PL_FLAG_BOT) &&
        !(selected.flags & ABCUI_PL_FLAG_LOCAL) && available;
    const char *labelToken = "#PL_Mute";
    if (!available)
      labelToken = "#PL_MuteUnavailable";
    else if (selected.structSize != 0 && (selected.flags & ABCUI_PL_FLAG_MUTED))
      labelToken = "#PL_Unmute";
    if (AbcUiElement *text = node("pl-mute-label"))
      Api->ui->elementSetInnerRML(text,
                                  escapeText(resolve(labelToken)).c_str());
    else
      Api->ui->elementSetInnerRML(mute,
                                  escapeText(resolve(labelToken)).c_str());
    Api->ui->elementSetClass(mute, "pl-disabled", mutableSelection ? 0 : 1);
    Api->ui->elementSetAttribute(mute, "data-action",
                                 mutableSelection ? kActionMute : nullptr);
  }

  if (AbcUiElement *status = node("pl-status")) {
    const std::string line = Api->playerlist->muteAvailable()
                                 ? std::string()
                                 : resolve("#PL_MuteUnavailable");
    Api->ui->elementSetInnerRML(status, escapeText(line.c_str()).c_str());
    Api->ui->elementSetProperty(status, "display",
                                line.empty() ? "none" : "block");
  }
}

// The safe default focus: the first row when one exists, else the close
// button (the module re-takes it on every fresh presentation).
void refocusDefault() {
  if (Api->playerlist && Api->playerlist->rosterCount() > 0) {
    if (AbcUiElement *list = node("pl-list")) {
      if (AbcUiElement *first = Api->ui->elementChild(list, 0)) {
        Api->ui->elementFocus(first);
        return;
      }
    }
  }
  if (AbcUiElement *close = node("pl-close"))
    Api->ui->elementFocus(close);
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
  if (!entry || action.empty() || !Api->playerlist)
    return 0;

  if (action == kActionSelect) {
    // DOM attributes are decoded once here. Identity crosses the section
    // boundary as values, not a colon-delimited/truncated command string.
    const int slot = Api->ui->elementAttributeInt(entry, "data-index", 0);
    const std::string steam = attributeOf(entry, "data-steamid");
    const unsigned long long steamId64 =
        steam.empty() ? 0 : std::strtoull(steam.c_str(), nullptr, 10);
    const std::string name = attributeOf(entry, "data-name");
    Api->playerlist->refresh();
    Api->playerlist->select(steamId64, slot, name.c_str());
    render();
    return 1;
  }
  if (action == kActionMute) {
    int32_t mutedAfter = 0;
    switch (Api->playerlist->toggleMute(&mutedAfter)) {
    case ABCUI_PL_TOGGLE_OK:
      Api->core->print("player mute %s: idx=%d now=%d\n",
                       mutedAfter ? "MUTED" : "UNMUTED", selectedSlot(),
                       mutedAfter);
      break;
    case ABCUI_PL_TOGGLE_UNAVAILABLE:
      Api->core->warn("player mute unavailable (voice service not "
                      "initialized)\n");
      break;
    case ABCUI_PL_TOGGLE_NO_SELECTION:
      Api->core->warn("player mute refused (no selection)\n");
      break;
    case ABCUI_PL_TOGGLE_IDENTITY_CHANGED:
      Api->core->warn(
          "player mute refused (selected player left or is a bot)\n");
      break;
    case ABCUI_PL_TOGGLE_READBACK_FAILED:
      Api->core->warn("player mute refused (backend did not change state)\n");
      break;
    case ABCUI_PL_TOGGLE_LOCAL_PLAYER:
      Api->core->warn("player mute refused (local player)\n");
      break;
    default: // ABCUI_PL_TOGGLE_STALE: the model refreshed and re-answers
      break;
    }
    render();
    return 1;
  }
  if (action == kActionBack) {
    Api->menu->back();
    return 1;
  }
  return 1; // an action-carrying element always consumes the click
}

void present() {
  if (!Page || !Api->playerlist)
    return;
  if (Api->documents->visible(Page))
    return;
  // Fresh projection: live roster read, then show (focus needs the document
  // visible), then the rows.
  Api->playerlist->refresh();
  RenderedRevision = ~0u;
  Api->documents->show(Page);
  refocusDefault();
  render();
}

void dismiss() {
  if (Page && Api->documents->visible(Page))
    Api->documents->hide(Page);
}

bool wantsPage(const AbcUiMenuState *state) {
  return state && state->up && state->page == ABCUI_MENU_PAGE_PLAYER_LIST;
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
  // The roster and every muted state are re-read per frame while the page
  // is up (the shipped dialog refreshed on a timer; a read-through keeps
  // the displayed mute labels honest with zero cache). The render stays
  // change-gated on the model revision.
  Api->playerlist->refresh();
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
  // A reopened page re-projects from scratch (the roster may have changed
  // while the page was away).
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

} // namespace playerlistpage
