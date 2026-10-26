// The find-servers page (moved from panels::serverbrowser). A projection of
// the framework's query session through the sdk serverbrowser section,
// exactly like the built-in page: nothing here writes an engine value, and
// no game state is inferred from the DOM. The framework owns the query
// lifecycle, the filter truth (the master pairs), the Steam stores and the
// blacklist save sink; the page handles its own view state (row selection,
// sort, paging, the detail window, the row menu, the auto-retry toggle)
// against the section and reports the fixed intents back through it.
//
// Rendering rules carried over (the T18a record): every server-derived
// string is escaped before it reaches the DOM; unknown facts stay visibly
// unknown ("-" ping, "?" players); the bounded page window is the render
// cap; the per-beat rebuild is signature-gated (including a digest over
// the visible rows and the read-through favorite state).
#include "../shared/PageShared.h"
#include "ProjectionSignature.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>

namespace serverbrowserpage {
namespace {

constexpr const char *kDocumentName = "menuserverbrowser";
constexpr const char *kDocumentPath =
    "mod://abcui.gameui/gameui/rml/serverbrowser.rml";

AbcUiDocument *Page = nullptr;
AbcUiElement *BoundRoot = nullptr;
uint64_t ClickBinding = 0;
uint64_t MousedownBinding = 0;
uint64_t ChangeBinding = 0;
uint64_t KeydownBinding = 0;
AbcUiSubscription StateSubscription = 0;
AbcUiSubscription PaintSubscription = 0;

// The page's own view state.
bool DetailsOpen = false;   // the detail window is up (a modal subpage)
bool MenuOpen = false;      // the row's secondary menu is up
bool AutoRetryJoin = false; // the action bar's toggle (off by default)
bool InControls = false;    // the controls projection re-entry guard
std::string RuleStatus;     // the blacklist editor's own feedback
bool Projected = false;     // the projection DOM matches the state
std::size_t LastSignature = 0;
bool HaveSignature = false;
float MenuScale = 1.0f; // the document's dp ratio (the menu clamp)

AbcUiElement *node(const char *id) {
  return Page ? Api->ui->querySelector(Page, id) : nullptr;
}

std::string inputValue(const char *id) {
  AbcUiElement *control = node(id);
  if (!control)
    return {};
  return moduleui::readString(Api->ui->elementValue, control);
}

void setInnerRml(const char *id, const std::string &rml) {
  if (AbcUiElement *element = node(id))
    Api->ui->elementSetInnerRML(element, rml.c_str());
}

void setDisabled(const char *id, bool disabled) {
  if (AbcUiElement *element = node(id))
    Api->ui->elementSetClass(element, "sb-disabled", disabled ? 1 : 0);
}

// Token resolution: our own table first; an unknown token passes through as
// raw text (visible honest fallback - never silently blanked).
std::string token(const char *name) {
  const std::string out = translate(name);
  return out.empty() ? std::string("#") + name : out;
}

std::string tokenValue(const char *name, int value) {
  std::string out = translate(name);
  if (out.empty())
    out = std::string("#") + name; // raw fallback keeps the count visible
  const std::string marker = "%d";
  const auto at = out.find(marker);
  if (at != std::string::npos)
    out.replace(at, marker.size(), std::to_string(value));
  return out;
}

std::string playersCell(const AbcUiSbEntry &entry) {
  if (!entry.responded)
    return "?";
  return std::format("{}/{}", entry.players, entry.maxPlayers);
}

std::string botsCell(const AbcUiSbEntry &entry) {
  if (!entry.responded)
    return "?";
  return std::to_string(entry.botPlayers);
}

std::string pingCell(const AbcUiSbEntry &entry) {
  if (!entry.responded || entry.pingMs < 0)
    return "-";
  return std::format("{}", entry.pingMs);
}

std::string addressCell(const AbcUiSbEntry &entry) {
  if (entry.ip == 0)
    return "-";
  const int port =
      entry.connectionPort ? entry.connectionPort : entry.queryPort;
  std::string out =
      std::format("{}.{}.{}.{}", entry.ip >> 24, (entry.ip >> 16) & 0xff,
                  (entry.ip >> 8) & 0xff, entry.ip & 0xff);
  if (port)
    out += ":" + std::to_string(port);
  return out;
}

// One "label: value" line of the detail window.
std::string detailLine(const char *labelToken, const std::string &value) {
  return "<div class=\"sb-detail-line\"><span class=\"k\">" +
         escapeText(token(labelToken)) + "</span><span class=\"v\">" +
         escapeText(value) + "</span></div>";
}

std::string playTime(int seconds) {
  if (seconds < 0)
    seconds = 0;
  if (seconds >= 3600)
    return std::format("{}:{:02d}:{:02d}", seconds / 3600, (seconds / 60) % 60,
                       seconds % 60);
  return std::format("{}:{:02d}", seconds / 60, seconds % 60);
}

// The blacklist's "blocked at" cell. A rule written before the timestamp
// existed stays visibly unknown ("-") - never an invented date.
std::string blockedAtCell(uint64_t seconds) {
  constexpr uint64_t kMinimum = 946684800ull;  // 2000-01-01
  constexpr uint64_t kMaximum = 4102444800ull; // 2100-01-01 (the store bound)
  if (seconds < kMinimum || seconds > kMaximum)
    return "-";
  const std::time_t stamp = static_cast<std::time_t>(seconds);
  std::tm parts{};
  if (localtime_s(&parts, &stamp) != 0)
    return "-";
  return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}", parts.tm_year + 1900,
                     parts.tm_mon + 1, parts.tm_mday, parts.tm_hour,
                     parts.tm_min);
}

const char *tabActionOf(int kind) {
  switch (kind) {
  case ABCUI_SB_TAB_LAN:
    return "sb-tab-lan";
  case ABCUI_SB_TAB_FAVORITES:
    return "sb-tab-favorites";
  case ABCUI_SB_TAB_HISTORY:
    return "sb-tab-history";
  case ABCUI_SB_TAB_FRIENDS:
    return "sb-tab-friends";
  default:
    return "sb-tab-internet";
  }
}

void renderControls();

void render() {
  if (!Page || !Api->documents->visible(Page) || !Api->serverbrowser)
    return;

  // --- status line (the module bar's right half) --------------------------
  std::string status;
  switch (Api->serverbrowser->status()) {
  case ABCUI_SB_STATUS_QUERYING:
    status = tokenValue("SB_Querying", Api->serverbrowser->serverCount());
    break;
  case ABCUI_SB_STATUS_COMPLETED:
    status =
        Api->serverbrowser->noServersListed() ||
                Api->serverbrowser->serverCount() == 0
            ? token("SB_NoServers")
            : tokenValue("SB_Completed", Api->serverbrowser->serverCount());
    break;
  case ABCUI_SB_STATUS_CANCELLED:
    status = tokenValue("SB_Cancelled", Api->serverbrowser->serverCount());
    break;
  case ABCUI_SB_STATUS_FAILED:
    switch (Api->serverbrowser->failure()) {
    case ABCUI_SB_FAILURE_UNAVAILABLE:
      status = token("SB_FailUnavailable");
      break;
    case ABCUI_SB_FAILURE_REJECTED:
      status = token("SB_FailRejected");
      break;
    case ABCUI_SB_FAILURE_TIMED_OUT:
      status = token("SB_FailTimedOut");
      break;
    default:
      status = token("SB_FailMaster");
      break;
    }
    break;
  default:
    break; // idle: no status line
  }
  const bool blocked = Api->serverbrowser->blockedOnly() != 0;
  if (!RuleStatus.empty())
    status = RuleStatus; // the blacklist editor's own feedback wins
  else if (blocked)
    status.clear(); // the list query's count is noise in the rules view
  setInnerRml("sb-status", escapeText(status));
  // The module bar's left half: the visible row count of the active view
  // (the blacklist view counts blocked rules, not servers).
  setInnerRml("sb-count",
              escapeText(blocked ? tokenValue("SB_BlacklistCount",
                                              Api->serverbrowser->rowCount())
                                 : tokenValue("SB_ServerCount",
                                              Api->serverbrowser->rowCount())));

  // --- tabs ---------------------------------------------------------------
  for (int kind = ABCUI_SB_TAB_INTERNET; kind <= ABCUI_SB_TAB_FRIENDS; ++kind) {
    if (AbcUiElement *tab = node(tabActionOf(kind)))
      Api->ui->elementSetClass(
          tab, "sb-active",
          kind == Api->serverbrowser->kind() && !blocked ? 1 : 0);
  }
  if (AbcUiElement *tab = node("sb-tab-blacklist"))
    Api->ui->elementSetClass(tab, "sb-active", blocked ? 1 : 0);

  // --- the action bar -----------------------------------------------------
  // Refresh doubles as Stop while a list query is live: the shipped
  // browser's own Refresh/Stop discipline keeps cancellation reachable
  // without adding a fifth button.
  if (AbcUiElement *refresh = node("sb-refresh")) {
    const bool live = Api->serverbrowser->status() == ABCUI_SB_STATUS_QUERYING;
    Api->ui->elementSetAttribute(refresh, "data-action",
                                 live ? "sb-stop" : "sb-refresh");
    Api->ui->elementSetInnerRML(
        refresh, escapeText(token(live ? "SB_Stop" : "SB_Refresh")).c_str());
    setDisabled("sb-refresh", false);
  }
  // The auto-retry is a toggle: the lit state is the player's own switch.
  if (AbcUiElement *toggle = node("sb-auto-retry"))
    Api->ui->elementSetClass(toggle, "sb-toggled", AutoRetryJoin ? 1 : 0);

  // --- sort markers -------------------------------------------------------
  const auto mark = [&](const char *id, int column) {
    if (AbcUiElement *element = node(id)) {
      const bool active = Api->serverbrowser->sortColumn() == column;
      Api->ui->elementSetClass(
          element, "sb-sort-asc",
          active && Api->serverbrowser->sortAscending() ? 1 : 0);
      Api->ui->elementSetClass(
          element, "sb-sort-desc",
          active && !Api->serverbrowser->sortAscending() ? 1 : 0);
    }
  };
  mark("sb-col-name", ABCUI_SB_SORT_NAME);
  mark("sb-col-map", ABCUI_SB_SORT_MAP);
  mark("sb-col-players", ABCUI_SB_SORT_PLAYERS);
  mark("sb-col-ping", ABCUI_SB_SORT_PING);

  // --- rows (the bounded page window) -------------------------------------
  const int selectedRow = Api->serverbrowser->selectedRow();
  std::string rows;
  for (int row = 0; row < ABCUI_SB_PAGE_SIZE; ++row) {
    AbcUiSbEntry entry;
    entry.structSize = sizeof(entry);
    if (!Api->serverbrowser->entryAt(row, &entry))
      break;
    rows += "<div class=\"sb-row";
    if (row == selectedRow)
      rows += " sb-selected";
    rows += "\" data-action=\"sb-row\" data-row=\"";
    rows += std::to_string(row);
    rows += "\"><span class=\"c-vac\">";
    if (entry.responded && entry.password)
      rows +=
          "<img class=\"server-icon\" src=\"../img/server-lock.png\" title=\"" +
          escapeAttribute(token("SB_PasswordYes")) + "\"/>";
    if (entry.responded && entry.secure)
      rows +=
          "<img class=\"server-icon\" src=\"../img/server-vac.png\" title=\"" +
          escapeAttribute(token("SB_SecureYes")) + "\"/>";
    rows += "</span><span class=\"c-name\">";
    rows += escapeText(entry.name);
    rows += "</span><span class=\"c-map\">";
    rows += escapeText(entry.map);
    rows += "</span><span class=\"c-players\">";
    rows += playersCell(entry);
    rows += "</span><span class=\"c-bots\">";
    rows += botsCell(entry);
    rows += "</span><span class=\"c-ping\">";
    rows += pingCell(entry);
    rows += "</span></div>";
  }
  if (rows.empty())
    rows =
        "<p class=\"sb-empty\">" + escapeText(token("SB_EmptyList")) + "</p>";
  setInnerRml("sb-list", rows);

  // --- paging -------------------------------------------------------------
  setDisabled("sb-page-prev", Api->serverbrowser->page() <= 0);
  setDisabled("sb-page-next", Api->serverbrowser->page() >=
                                  Api->serverbrowser->pageCount() - 1);
  const std::string pageLabel =
      std::format("{} / {}", Api->serverbrowser->page() + 1,
                  Api->serverbrowser->pageCount());
  setInnerRml("sb-page-label", escapeText(pageLabel));

  // --- the detail window's content (the CURRENT single slot only) ---------
  AbcUiSbEntry selected;
  bool haveSelected = false;
  if (selectedRow >= 0)
    haveSelected = Api->serverbrowser->entryAt(selectedRow, &selected) != 0;
  if (!haveSelected) {
    setInnerRml("sb-detail-info", "<p class=\"sb-hint\">" +
                                      escapeText(token("SB_DetailHint")) +
                                      "</p>");
    setInnerRml("sb-detail-players", "");
    setDisabled("sb-connect", true);
  } else {
    std::string info = "<div id=\"sb-detail-name\">";
    info += escapeText(selected.name);
    info += "</div>";
    info += detailLine("SB_Address", addressCell(selected));
    info += detailLine("SB_ColumnMap", selected.map);
    info += detailLine("SB_ColumnPlayers", playersCell(selected));
    info += detailLine("SB_ColumnBots", botsCell(selected));
    info += detailLine("SB_ColumnPing", pingCell(selected));
    if (selected.gameDir[0] != '\0')
      info += detailLine("SB_Game", selected.gameDir);
    std::string security = token("SB_SecureYes");
    if (selected.password)
      security = token("SB_PasswordYes") + " | " + security;
    info += detailLine("SB_Flags", security);
    setInnerRml("sb-detail-info", info);
    setDisabled("sb-connect", blocked || !selected.responded);

    std::string players;
    if (Api->serverbrowser->detailKind() == ABCUI_SB_DETAIL_PLAYERS &&
        Api->serverbrowser->detailStatus() == ABCUI_SB_STATUS_QUERYING)
      players = "<p class=\"sb-hint\">" +
                escapeText(token("SB_DetailQuerying")) + "</p>";
    else if (Api->serverbrowser->detailKind() == ABCUI_SB_DETAIL_PLAYERS &&
             Api->serverbrowser->detailStatus() == ABCUI_SB_STATUS_FAILED)
      players = "<p class=\"sb-hint\">" + escapeText(token("SB_DetailFailed")) +
                "</p>";
    else if (Api->serverbrowser->detailKind() == ABCUI_SB_DETAIL_PLAYERS &&
             Api->serverbrowser->detailStatus() == ABCUI_SB_STATUS_COMPLETED) {
      if (Api->serverbrowser->playerCount() == 0)
        players = "<p class=\"sb-hint\">" + escapeText(token("SB_NoPlayers")) +
                  "</p>";
      else
        players = "<div class=\"sb-player-head\"><span class=\"dp-name\">" +
                  escapeText(token("SB_PlayerName")) +
                  "</span><span class=\"dp-score\">" +
                  escapeText(token("SB_Score")) +
                  "</span><span class=\"dp-time\">" +
                  escapeText(token("SB_Time")) + "</span></div>";
      for (int i = 0; i < Api->serverbrowser->playerCount(); ++i) {
        AbcUiSbPlayer player;
        player.structSize = sizeof(player);
        if (!Api->serverbrowser->playerAt(i, &player))
          continue;
        players += "<div class=\"sb-player\"><span class=\"dp-name\">";
        players += escapeText(player.name);
        players += "</span><span class=\"dp-score\">";
        players += std::to_string(player.score);
        players += "</span><span class=\"dp-time\">";
        players += playTime(player.seconds);
        players += "</span></div>";
      }
    }
    setInnerRml("sb-detail-players", players);
  }

  renderControls();
}

// The sub-state projection: the blacklist view, the filter column's values,
// the detail window's visibility and the row menu. The handlers below can
// call it directly for immediate feedback (its own writes never re-enter).
void renderControls() {
  if (!Page || !Api->serverbrowser || InControls)
    return;
  InControls = true;
  const bool blocked = Api->serverbrowser->blockedOnly() != 0;

  if (AbcUiElement *module = node("sb-list-module"))
    Api->ui->elementSetClass(module, "sb-blacklist-mode", blocked ? 1 : 0);
  if (AbcUiElement *panel = node("sb-blacklist-panel"))
    Api->ui->elementSetClass(panel, "sb-open", blocked ? 1 : 0);
  if (AbcUiElement *panel = node("sb-filter-module"))
    Api->ui->elementSetClass(panel, "sb-hidden", blocked ? 1 : 0);

  // The filter column's projection (the model is the one truth; a focused
  // text control keeps what the user is typing until the change commits).
  AbcUiSbFilter filter{};
  filter.structSize = sizeof(filter);
  Api->serverbrowser->filter(&filter);
  const auto syncText = [&](const char *id, const char *value) {
    if (AbcUiElement *control = node(id)) {
      if (!Api->ui->elementPseudoClassSet(control, "focus") &&
          inputValue(id) != value)
        Api->ui->elementSetValue(control, value);
    }
  };
  const auto syncSelect = [&](const char *id, const std::string &value) {
    // Programmatic select writes dispatch a change event in RmlUi, so the
    // value is only written when the control actually moved (the change
    // handler re-checks the model value before acting - no feedback loop).
    if (AbcUiElement *select = node(id)) {
      if (inputValue(id) != value)
        Api->ui->elementSetValue(select, value.c_str());
    }
  };
  syncText("sb-filter-name", filter.name);
  syncText("sb-filter-map", filter.map);
  syncSelect("sb-filter-ping", std::to_string(filter.maxPing));
  syncSelect("sb-filter-empty", filter.hideEmpty ? "1" : "0");
  syncSelect("sb-filter-full", filter.hideFull ? "1" : "0");
  syncSelect("sb-filter-secure", filter.secureOnly ? "1" : "0");
  syncSelect("sb-filter-password", filter.noPassword ? "1" : "0");

  // The detail window: a modal subpage. Its content is projected by
  // render(); this only moves the visibility.
  if (AbcUiElement *overlay = node("sb-detail-overlay"))
    Api->ui->elementSetClass(overlay, "sb-open", DetailsOpen ? 1 : 0);

  // The row menu: the favorite entry reads the store through (Steam is the
  // one truth), and an unavailable store leaves the entry inert.
  if (AbcUiElement *menu = node("sb-context-menu"))
    Api->ui->elementSetClass(menu, "sb-open", MenuOpen ? 1 : 0);
  const int selectedFavorite = Api->serverbrowser->selectedFavorite();
  if (AbcUiElement *entry = node("sb-menu-favorite")) {
    Api->ui->elementSetInnerRML(
        entry, escapeText(token(selectedFavorite == 1 ? "SB_FavoriteRemove"
                                                      : "SB_FavoriteAdd"))
                   .c_str());
    Api->ui->elementSetClass(entry, "sb-disabled",
                             Api->serverbrowser->favoritesAvailable() ? 0 : 1);
  }
  if (AbcUiElement *entry = node("sb-menu-join"))
    Api->ui->elementSetClass(entry, "sb-disabled", blocked ? 1 : 0);
  if (AbcUiElement *entry = node("sb-menu-block"))
    Api->ui->elementSetClass(entry, "sb-disabled", blocked ? 1 : 0);
  if (AbcUiElement *entry = node("sb-menu-block-ip"))
    Api->ui->elementSetClass(entry, "sb-disabled", blocked ? 1 : 0);

  // The blacklist editor.
  if (AbcUiElement *rules = node("sb-blacklist-rules")) {
    std::string markup;
    const int32_t count = Api->serverbrowser->ruleCount();
    for (int32_t index = 0; index < count; ++index) {
      AbcUiSbRule rule;
      rule.structSize = sizeof(rule);
      if (!Api->serverbrowser->ruleAt(index, &rule))
        continue;
      // The Source dialog's shape: the blocked server, its address and the
      // moment it was blocked. A rule with no recorded name stays visibly
      // unnamed instead of repeating its address as a label.
      markup += "<div class=\"sb-blacklist-rule\"><span class=\"bl-name\">" +
                escapeText(rule.name[0] ? rule.name : "-") +
                "</span><span "
                "class=\"bl-ip\">" +
                escapeText(rule.address) + "</span><span class=\"bl-time\">" +
                escapeText(blockedAtCell(rule.blockedAt)) +
                "</span><span class=\"bl-action\"><button "
                "data-action=\"sb-rule-remove\" data-address=\"" +
                escapeAttribute(rule.address) + "\">" +
                escapeText(token("SB_Unblock")) + "</button></span></div>";
    }
    Api->ui->elementSetInnerRML(rules, markup.c_str());
  }
  InControls = false;
}

// The filter selects carry fixed option sets; the labels are localized once
// per document load (the projection then only moves the selection).
void buildFilterOptions() {
  const auto fillYesNo = [&](const char *id) {
    AbcUiElement *select = node(id);
    if (!select)
      return;
    Api->ui->elementSelectRemoveAll(select);
    Api->ui->elementSelectAdd(select, escapeText(token("SB_Any")).c_str(), "0");
    Api->ui->elementSelectAdd(select, escapeText(token("SB_Yes")).c_str(), "1");
    Api->ui->elementSelectSetSelection(select, 0);
  };
  for (const char *id : {"sb-filter-empty", "sb-filter-full",
                         "sb-filter-secure", "sb-filter-password"})
    fillYesNo(id);
  if (AbcUiElement *ping = node("sb-filter-ping")) {
    Api->ui->elementSelectRemoveAll(ping);
    for (int value : {0, 50, 100, 150, 250, 350, 500}) {
      const std::string text =
          value ? std::to_string(value) + " ms" : escapeText(token("SB_Any"));
      Api->ui->elementSelectAdd(ping, text.c_str(),
                                std::to_string(value).c_str());
    }
    Api->ui->elementSelectSetSelection(ping, 0);
  }
}

// --- interaction
// ---------------------------------------------------------------

void closeContextMenu() {
  if (!MenuOpen)
    return;
  MenuOpen = false;
  Projected = false;
  renderControls();
}

void closeDetails() {
  if (!DetailsOpen)
    return;
  DetailsOpen = false;
  Api->serverbrowser->closeDetail(); // the window is the only slot consumer
  Projected = false;
  renderControls();
}

void refocusDefault() {
  if (AbcUiElement *fallback = node("sb-refresh"))
    Api->ui->elementFocus(fallback);
}

// Opens the row's secondary menu at the click point, clamped into the
// viewport (the menu is 132dp wide and 4 x 20dp tall plus padding).
void openContextMenu(float pointX, float pointY) {
  AbcUiElement *menu = node("sb-context-menu");
  if (!menu || !Page)
    return;
  MenuOpen = true;
  int viewportW = 0;
  int viewportH = 0;
  Api->ui->documentSize(Page, &viewportW, &viewportH);
  const int width = static_cast<int>(132.0f * MenuScale) + 6;
  const int height = static_cast<int>((4 * 20 + 4 + 2) * MenuScale) + 6;
  int left = static_cast<int>(pointX);
  int top = static_cast<int>(pointY);
  if (viewportW > 0 && left + width > viewportW)
    left = static_cast<int>(pointX) - width;
  if (viewportH > 0 && top + height > viewportH)
    top = static_cast<int>(pointY) - height;
  if (left < 0)
    left = 0;
  if (top < 0)
    top = 0;
  const std::string leftText = std::format("{}px", left);
  Api->ui->elementSetProperty(menu, "left", leftText.c_str());
  const std::string topText = std::format("{}px", top);
  Api->ui->elementSetProperty(menu, "top", topText.c_str());
  Api->ui->elementSetClass(menu, "sb-open", 1);
}

bool insideNode(AbcUiElement *nodeElement, AbcUiElement *container) {
  for (AbcUiElement *it = nodeElement; it; it = Api->ui->elementParent(it)) {
    if (it == container)
      return true;
  }
  return false;
}

// The nearest row element (the right-click path needs the row under the
// cursor, not the element that happened to carry the action attribute).
AbcUiElement *rowNodeOf(AbcUiElement *element) {
  for (AbcUiElement *it = element; it; it = Api->ui->elementParent(it)) {
    if (!attributeOf(it, "data-row").empty())
      return it;
  }
  return nullptr;
}

// The blacklist mutations (page-owned rules; the save sink stays
// framework-side).
bool handleBlacklistAction(AbcUiElement *entry, const std::string &action) {
  if (action != "sb-block" && action != "sb-block-ip" &&
      action != "sb-rule-add" && action != "sb-rule-remove")
    return false;
  bool ok = false;
  if (action == "sb-rule-remove") {
    ok = Api->serverbrowser->removeRule(
             attributeOf(entry, "data-address").c_str()) != 0;
  } else if (action == "sb-rule-add") {
    ok = Api->serverbrowser->blockAddress(
             inputValue("sb-blacklist-address").c_str()) != 0;
  } else {
    ok =
        Api->serverbrowser->blockSelected(action == "sb-block-ip" ? 1 : 0) != 0;
  }
  RuleStatus = translate(ok ? "SB_BlacklistSaved" : "SB_BlacklistError");
  if (RuleStatus.empty())
    RuleStatus =
        std::string("#") + (ok ? "SB_BlacklistSaved" : "SB_BlacklistError");
  Projected = false;
  renderControls();
  return true;
}

// The docked filter column's change events. Text and ping filters apply
// client-side only; the three master-pair toggles report the change to the
// framework (which re-issues the query). Every path re-checks the model
// value first: a projection write that echoes the same value is a no-op,
// so the select's own change echo cannot loop.
void handleChange(AbcUiElement *target, const std::string &value) {
  if (!target || InControls)
    return;
  const char *id = Api->ui->elementId(target);
  if (!id)
    return;
  if (value.size() > 256)
    return;
  AbcUiSbFilter filter{};
  filter.structSize = sizeof(filter);
  Api->serverbrowser->filter(&filter);
  bool changed = false;
  bool masterPair = false;
  if (std::string_view(id) == "sb-filter-name") {
    changed = std::string_view(filter.name) != value;
    copyCString(filter.name, value);
  } else if (std::string_view(id) == "sb-filter-map") {
    changed = std::string_view(filter.map) != value;
    copyCString(filter.map, value);
  } else if (std::string_view(id) == "sb-filter-ping") {
    const auto parsed = parseInt(value);
    if (!parsed || *parsed < 0 || *parsed > 500)
      return;
    changed = filter.maxPing != *parsed;
    filter.maxPing = *parsed;
  } else if (std::string_view(id) == "sb-filter-password") {
    const bool on = value == "1";
    changed = filter.noPassword != on;
    filter.noPassword = on;
  } else if (std::string_view(id) == "sb-filter-empty" ||
             std::string_view(id) == "sb-filter-full" ||
             std::string_view(id) == "sb-filter-secure") {
    const bool on = value == "1";
    if (std::string_view(id) == "sb-filter-empty") {
      changed = filter.hideEmpty != on;
      filter.hideEmpty = on;
    } else if (std::string_view(id) == "sb-filter-full") {
      changed = filter.hideFull != on;
      filter.hideFull = on;
    } else {
      changed = filter.secureOnly != on;
      filter.secureOnly = on;
    }
    masterPair = true;
  } else {
    return;
  }
  if (!changed)
    return;
  Api->serverbrowser->setFilter(&filter);
  Projected = false;
  if (masterPair) {
    // The master-side pair moved: the framework re-issues the list query.
    Api->serverbrowser->applyFilters();
  }
}

// ESC on the page closes one subpage: the row menu first, then the detail
// window. The claim hands the press back (leave the page closes separately).
void closeSubpage() {
  if (MenuOpen) {
    closeContextMenu();
    refocusDefault();
    return;
  }
  if (DetailsOpen) {
    closeDetails();
    refocusDefault();
  }
}

int32_t onKeydown(void *, AbcUiElement *target,
                  const AbcUiElementEvent *event) {
  if (!event || std::string_view(event->type) != "keydown")
    return 0;
  if (event->keynum != ABCUI_KEY_ESCAPE)
    return 0;
  if (!MenuOpen && !DetailsOpen)
    return 0;
  // One key closes one subpage; the claim keeps this press from also
  // leaving the page (the menu owner's routed ESC reads it).
  Api->serverbrowser->noteEscapeClaim();
  closeSubpage();
  return 1;
}

int32_t onMousedown(void *, AbcUiElement *target,
                    const AbcUiElementEvent *event) {
  if (!event || !target || std::string_view(event->type) != "mousedown")
    return 0;
  if (event->button != 1)
    return 0;
  if (DetailsOpen || Api->serverbrowser->blockedOnly())
    return 0;
  AbcUiElement *row = rowNodeOf(target);
  if (!row)
    return 0;
  const int browserRow = parseInt(attributeOf(row, "data-row")).value_or(0);
  Api->serverbrowser->selectRow(browserRow, 0); // a view action: no detail
  openContextMenu(event->mouseX, event->mouseY);
  if (AbcUiElement *first = node("sb-menu-join"))
    Api->ui->elementFocus(first);
  Projected = false;
  return 0;
}

int32_t onChange(void *, AbcUiElement *target, const AbcUiElementEvent *event) {
  if (!event || std::string_view(event->type) != "change")
    return 0;
  handleChange(target, inputValue(Api->ui->elementId(target)));
  return 0;
}

int32_t onClick(void *, AbcUiElement *target, const AbcUiElementEvent *event) {
  if (!event || !target || std::string_view(event->type) != "click")
    return 0;
  AbcUiElement *menu = node("sb-context-menu");
  const bool insideMenu = menu && insideNode(target, menu);
  if (!insideMenu)
    closeContextMenu(); // any click outside the menu dismisses it
  AbcUiElement *entry = target;
  std::string action;
  while (entry) {
    action = attributeOf(entry, "data-action");
    if (!action.empty())
      break;
    entry = Api->ui->elementParent(entry);
  }

  if (insideMenu && MenuOpen) {
    // The secondary menu entries: join/favorite travel to the framework,
    // the blacklist mutations stay page-owned. A dimmed entry is inert.
    if (entry && !action.empty() &&
        !Api->ui->elementClassSet(entry, "sb-disabled")) {
      if (action == "sb-connect") {
        Api->serverbrowser->connect();
      } else if (action == "sb-favorite-toggle") {
        Api->serverbrowser->toggleFavorite();
      } else if (action == "sb-block" || action == "sb-block-ip") {
        handleBlacklistAction(entry, action);
      }
    }
    closeContextMenu();
    return 1;
  }

  if (DetailsOpen) {
    // A modal subpage: only its own bottom row answers; every other click
    // inside the background is inert. Join joins the shown server through
    // the ordinary connect intent (the selection is the shown server);
    // auto-retry arms the join auto-retry for the full-server case.
    if (action == "sb-detail-close") {
      closeDetails();
      refocusDefault();
    } else if (action == "sb-auto-retry") {
      AutoRetryJoin = !AutoRetryJoin;
      Api->serverbrowser->setAutoRetryJoin(AutoRetryJoin ? 1 : 0);
      Projected = false;
    } else if (action == "sb-connect") {
      Api->serverbrowser->connect();
    }
    return 1;
  }
  // The blacklist editor's own actions (page-owned rules).
  if (entry && (action == "sb-block" || action == "sb-block-ip" ||
                action == "sb-rule-add" || action == "sb-rule-remove")) {
    handleBlacklistAction(entry, action);
    return 1;
  }
  if (action == "sb-filters-reset") {
    // Reset restores every filter to its default; the game identity
    // (gamedir) and the cvar-owned unresponsive rule stay. The framework
    // re-issues the query so the master-side pairs follow.
    if (Api->serverbrowser->resetFilters())
      Api->serverbrowser->applyFilters();
    Projected = false;
    return 1;
  }
  if (action == "sb-tab-blacklist") {
    Api->serverbrowser->setBlockedOnly(1);
    Projected = false;
    return 1;
  }
  if (action.starts_with("sb-tab-")) {
    RuleStatus.clear();
    Api->serverbrowser->setBlockedOnly(0);
    // fall through to the intent dispatch below
  }

  // Page-internal view state first: selection, sort, paging (no framework
  // trip; the re-render is the next beat's - the DOM is never rebuilt from
  // inside its own event dispatch).
  if (action == "sb-row") {
    const int row = parseInt(attributeOf(entry, "data-row")).value_or(0);
    Api->serverbrowser->selectRow(row, 1); // the left click opens the window
    DetailsOpen = Api->serverbrowser->selectedRow() >= 0;
    Projected = false;
    return 1;
  }
  if (action == "sb-sort") {
    const std::string column = attributeOf(entry, "data-column");
    int sort = ABCUI_SB_SORT_NAME;
    if (column == "map")
      sort = ABCUI_SB_SORT_MAP;
    else if (column == "players")
      sort = ABCUI_SB_SORT_PLAYERS;
    else if (column == "ping")
      sort = ABCUI_SB_SORT_PING;
    if (Api->serverbrowser->sortColumn() == sort)
      Api->serverbrowser->setSort(sort,
                                  Api->serverbrowser->sortAscending() ? 0 : 1);
    else
      Api->serverbrowser->setSort(sort, 1);
    Projected = false;
    return 1;
  }
  if (action == "sb-page-prev") {
    Api->serverbrowser->pagePrev();
    Projected = false;
    return 1;
  }
  if (action == "sb-page-next") {
    Api->serverbrowser->pageNext();
    Projected = false;
    return 1;
  }

  // Framework intents (fixed names; the framework owns the query lifecycle,
  // the master-filter truth and every engine/Steam mutation; the beat
  // re-projects).
  if (action == "sb-tab-internet") {
    Api->serverbrowser->setTab(ABCUI_SB_TAB_INTERNET);
    return 1;
  }
  if (action == "sb-tab-lan") {
    Api->serverbrowser->setTab(ABCUI_SB_TAB_LAN);
    return 1;
  }
  if (action == "sb-tab-favorites") {
    Api->serverbrowser->setTab(ABCUI_SB_TAB_FAVORITES);
    return 1;
  }
  if (action == "sb-tab-history") {
    Api->serverbrowser->setTab(ABCUI_SB_TAB_HISTORY);
    return 1;
  }
  if (action == "sb-tab-friends") {
    Api->serverbrowser->setTab(ABCUI_SB_TAB_FRIENDS);
    return 1;
  }
  if (action == "sb-refresh") {
    Api->serverbrowser->refresh();
    return 1;
  }
  if (action == "sb-stop") {
    Api->serverbrowser->stop();
    return 1;
  }
  if (action == "sb-connect") {
    Api->serverbrowser->connect();
    return 1;
  }
  if (action == "sb-favorite-toggle") {
    Api->serverbrowser->toggleFavorite();
    return 1;
  }
  if (action == "sb-filters-apply") {
    Api->serverbrowser->applyFilters();
    return 1;
  }
  if (action == "return") {
    Api->menu->back();
    return 1;
  }
  return 0;
}

// --- presentation
// -------------------------------------------------------------

void present() {
  if (!Page || !Api->serverbrowser)
    return;
  if (Api->documents->visible(Page))
    return;
  Api->documents->show(Page);
  refocusDefault();
  Api->ui->documentScale(Page, &MenuScale);
  Projected = false;
}

void dismiss() {
  if (Page && Api->documents->visible(Page))
    Api->documents->hide(Page);
}

bool wantsPage(const AbcUiMenuState *state) {
  return state && state->up && state->page == ABCUI_MENU_PAGE_SERVERS;
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
  // The per-beat projection: view-model sync when the snapshot moved, DOM
  // rebuild only when the projection signature changed.
  if (Api->serverbrowser->sync())
    Projected = false; // the view model moved: rebuild once
  const std::size_t signature =
      projectionSignature(DetailsOpen, MenuOpen, AutoRetryJoin);
  if (Projected && HaveSignature && signature == LastSignature)
    return;
  render();
  LastSignature = signature;
  HaveSignature = true;
  Projected = true;
}

void onDocumentReady(void *) {
  BoundRoot = Api->documents->root(Page);
  if (!BoundRoot)
    return;
  ClickBinding =
      Api->ui->elementAddListener(BoundRoot, "click", &onClick, nullptr);
  MousedownBinding = Api->ui->elementAddListener(BoundRoot, "mousedown",
                                                 &onMousedown, nullptr);
  ChangeBinding =
      Api->ui->elementAddListener(BoundRoot, "change", &onChange, nullptr);
  KeydownBinding = Api->ui->elementAddListenerCapture(BoundRoot, "keydown",
                                                      &onKeydown, nullptr);
  buildFilterOptions();
  Api->ui->documentScale(Page, &MenuScale);
  Projected = false;
  HaveSignature = false;
}

void onDocumentHide(void *) {
  Projected = false;
  HaveSignature = false;
}

void onDocumentClose(void *) {
  // The DOM is still alive: drop the listeners before it goes (the
  // framework owns the listener objects until they are removed).
  struct Binding {
    const char *type;
    uint64_t id;
    bool capture;
  };
  const Binding bindings[] = {
      {"click", ClickBinding, false},
      {"mousedown", MousedownBinding, false},
      {"change", ChangeBinding, false},
      {"keydown", KeydownBinding, true},
  };
  if (BoundRoot) {
    for (const Binding &binding : bindings) {
      if (!binding.id)
        continue;
      Api->ui->elementRemoveListener(BoundRoot, binding.type, binding.id);
    }
  }
  ClickBinding = MousedownBinding = ChangeBinding = KeydownBinding = 0;
  BoundRoot = nullptr;
  DetailsOpen = false;
  MenuOpen = false;
  Projected = false;
  HaveSignature = false;
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
  ClickBinding = MousedownBinding = ChangeBinding = KeydownBinding = 0;
  DetailsOpen = false;
  MenuOpen = false;
  AutoRetryJoin = false;
  RuleStatus.clear();
  Projected = false;
  HaveSignature = false;
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

} // namespace serverbrowserpage
