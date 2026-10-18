// panels::serverbrowser implementation - see ServerBrowserPage.h for the
// projection contract and the shipped-binary references behind the rules.
#include "ServerBrowserPage.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>

#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"
#include "../../services/serverbrowser/ServerListModel.h"

#include <abc/Console.h>

#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace panels::serverbrowser {

namespace {

using services::serverbrowser::QueryKind;
using services::serverbrowser::QueryStatus;
using services::serverbrowser::ServerEntry;

constexpr const char *kActionTabInternet = "sb-tab-internet";
constexpr const char *kActionTabLan = "sb-tab-lan";
constexpr const char *kActionTabFavorites = "sb-tab-favorites";
constexpr const char *kActionTabHistory = "sb-tab-history";
constexpr const char *kActionTabFriends = "sb-tab-friends";
constexpr const char *kActionRefresh = "sb-refresh";
constexpr const char *kActionStop = "sb-stop";
constexpr const char *kActionSort = "sb-sort";
constexpr const char *kActionRow = "sb-row";
constexpr const char *kActionPagePrev = "sb-page-prev";
constexpr const char *kActionPageNext = "sb-page-next";
constexpr const char *kActionConnect = "sb-connect";
constexpr const char *kActionFavoriteToggle = "sb-favorite-toggle";
constexpr const char *kActionReturn = "return";

Rml::Element *actionNode(Rml::Element *node) {
  for (Rml::Element *it = node; it; it = it->GetParentNode()) {
    if (it->HasAttribute("data-action"))
      return it;
  }
  return nullptr;
}

// The nearest row element (the right-click path needs the row under the
// cursor, not the element that happened to carry the action attribute).
Rml::Element *rowNode(Rml::Element *node) {
  for (Rml::Element *it = node; it; it = it->GetParentNode()) {
    if (it->HasAttribute("data-row"))
      return it;
  }
  return nullptr;
}

bool insideNode(Rml::Element *node, Rml::Element *container) {
  for (Rml::Element *it = node; it; it = it->GetParentNode()) {
    if (it == container)
      return true;
  }
  return false;
}

// Token resolution: our own table first; an unknown token passes through as
// raw text (visible honest fallback - never silently blanked, same rule the
// loading pages use for engine text).
std::string token(const char *name) {
  std::string out;
  if (framework::Localization::translate(out, name))
    return out;
  return std::string("#") + name;
}

std::string tokenValue(const char *name, int value) {
  std::string out;
  if (!framework::Localization::translate(out, name)) {
    out = std::string("#") + name; // raw fallback keeps the count visible
  }
  const std::string marker = "%d";
  const auto at = out.find(marker);
  if (at != std::string::npos)
    out.replace(at, marker.size(), std::to_string(value));
  return out;
}

void setHtml(Rml::ElementDocument *doc, const char *id,
             const std::string &html) {
  if (!doc)
    return;
  if (Rml::Element *node = doc->GetElementById(id))
    node->SetInnerRML(html);
}

void setDisabled(Rml::ElementDocument *doc, const char *id, bool disabled) {
  if (!doc)
    return;
  if (Rml::Element *node = doc->GetElementById(id))
    node->SetClass("sb-disabled", disabled);
}

// Programmatic select writes dispatch a change event in RmlUi, so the value
// is only written when the control actually moved (and the handlers below
// re-check the model value before acting - no projection feedback loop).
void syncSelect(Rml::ElementDocument *doc, const char *id,
                const std::string &value) {
  if (!doc)
    return;
  auto *select = rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(
      doc->GetElementById(id));
  if (!select || select->GetValue() == value.c_str())
    return;
  select->SetValue(value.c_str());
}

// FNV-1a byte mixin for the projection signature.
void mix(std::size_t &hash, const void *data, std::size_t size) {
  const unsigned char *bytes = static_cast<const unsigned char *>(data);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
}
template <typename T> void mixValue(std::size_t &hash, const T &value) {
  mix(hash, &value, sizeof(T));
}
void mixText(std::size_t &hash, const std::string &text) {
  mix(hash, text.data(), text.size());
}

std::string playersCell(const ServerEntry &entry) {
  if (!entry.responded)
    return "?";
  char buffer[24];
  _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%d/%d", entry.players,
              entry.maxPlayers);
  return buffer;
}

std::string botsCell(const ServerEntry &entry) {
  if (!entry.responded)
    return "?";
  return std::to_string(entry.botPlayers);
}

std::string pingCell(const ServerEntry &entry) {
  if (!entry.responded || entry.pingMs < 0)
    return "-";
  char buffer[16];
  _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%d", entry.pingMs);
  return buffer;
}

std::string addressCell(const ServerEntry &entry) {
  if (entry.ip == 0)
    return "-";
  const uint16_t port =
      entry.connectionPort ? entry.connectionPort : entry.queryPort;
  std::string text = services::serverbrowser::formatServerIp(entry.ip);
  if (port)
    text += ":" + std::to_string(port);
  return text;
}

// One "label: value" line of the detail window.
std::string detailLine(const char *labelToken, const std::string &value) {
  return "<div class=\"sb-detail-line\"><span class=\"k\">" +
         framework::TextUtil::escapeRml(token(labelToken)) +
         "</span><span class=\"v\">" + framework::TextUtil::escapeRml(value) +
         "</span></div>";
}

std::string playTime(int seconds) {
  if (seconds < 0)
    seconds = 0;
  char buffer[24];
  if (seconds >= 3600)
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%d:%02d:%02d",
                seconds / 3600, (seconds / 60) % 60, seconds % 60);
  else
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%d:%02d", seconds / 60,
                seconds % 60);
  return buffer;
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
  char buffer[32];
  _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%04d-%02d-%02d %02d:%02d",
              parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
              parts.tm_hour, parts.tm_min);
  return buffer;
}

const char *tabActionOf(QueryKind kind) {
  switch (kind) {
  case QueryKind::Internet:
    return kActionTabInternet;
  case QueryKind::Lan:
    return kActionTabLan;
  case QueryKind::Favorites:
    return kActionTabFavorites;
  case QueryKind::History:
    return kActionTabHistory;
  case QueryKind::Friends:
    return kActionTabFriends;
  default:
    return kActionTabInternet;
  }
}

} // namespace

ServerBrowserPage::ServerBrowserPage()
    : InteractiveDocument("menuserverbrowser",
                          "abc/abcui/gameui/rml/serverbrowser.rml", true),
      Listener(this) {}

ServerBrowserPage::~ServerBrowserPage() {
  close();
}

void ServerBrowserPage::onDocumentReady() {
  if (Rml::ElementDocument *doc = document()) {
    addListener(doc, "click", &Listener);
    addListener(doc, "mousedown", &Listener);
    addListener(doc, "change", &Listener);
  }
  buildFilterOptions();
  Projected = false;
  HaveSignature = false;
}

// The filter selects carry fixed option sets; the labels are localized once
// per document load (the projection then only moves the selection).
void ServerBrowserPage::buildFilterOptions() {
  Rml::ElementDocument *doc = document();
  if (!doc)
    return;
  const auto fillYesNo = [&](const char *id) {
    auto *select = rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(
        doc->GetElementById(id));
    if (!select)
      return;
    select->RemoveAll();
    select->Add(framework::TextUtil::escapeRml(token("SB_Any")).c_str(), "0");
    select->Add(framework::TextUtil::escapeRml(token("SB_Yes")).c_str(), "1");
    select->SetSelection(0);
  };
  for (const char *id : {"sb-filter-empty", "sb-filter-full",
                         "sb-filter-secure", "sb-filter-password"})
    fillYesNo(id);
  if (auto *ping = rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(
          doc->GetElementById("sb-filter-ping"))) {
    ping->RemoveAll();
    for (int value : {0, 50, 100, 150, 250, 350, 500}) {
      const std::string text =
          value ? std::to_string(value) + " ms"
                : framework::TextUtil::escapeRml(token("SB_Any"));
      ping->Add(text.c_str(), std::to_string(value));
    }
    ping->SetSelection(0);
  }
}

void ServerBrowserPage::tick() {
  if (!Query || !List || !document())
    return;
  if (List->sync(*Query))
    Projected = false; // the view model moved: rebuild once
  const std::size_t signature = projectionSignature();
  if (Projected && HaveSignature && signature == LastSignature)
    return;
  render();
  LastSignature = signature;
  HaveSignature = true;
  Projected = true;
}

std::size_t ServerBrowserPage::projectionSignature() const {
  std::size_t hash = 2166136261u;
  const auto &query = *Query;
  const auto &list = *List;
  mixValue(hash, static_cast<int>(query.status()));
  mixValue(hash, static_cast<int>(query.failure()));
  mixValue(hash, static_cast<int>(query.kind()));
  mixValue(hash, query.generation());
  mixValue(hash, query.count());
  mixValue(hash, query.noServersListed());
  mixValue(hash, static_cast<int>(query.detailKind()));
  mixValue(hash, static_cast<int>(query.detailStatus()));
  mixValue(hash, query.detailFailed());
  mixValue(hash, query.playerCount());
  mixValue(hash, list.rowCount());
  mixValue(hash, list.page());
  mixValue(hash, list.pageCount());
  mixValue(hash, static_cast<int>(list.sortColumn()));
  mixValue(hash, list.sortAscending());
  mixValue(hash, list.selectedKey());
  mixValue(hash, list.blockedOnly());
  mixValue(hash, DetailsOpen);
  mixValue(hash, MenuOpen);
  mixValue(hash, AutoRetryJoin);
  if (Blacklist)
    mixValue(hash, Blacklist->revision());
  const auto &filter = list.filter();
  mixValue(hash, filter.hideEmpty);
  mixValue(hash, filter.hideFull);
  mixValue(hash, filter.secureOnly);
  mixText(hash, filter.gameDir);
  mixText(hash, filter.Name);
  mixText(hash, filter.Map);
  mixValue(hash, filter.MaxPing);
  mixValue(hash, filter.NoPassword);
  mixValue(hash, filter.InsecureOnly);
  mixValue(hash, filter.HideUnresponsive);
  const int rows = services::serverbrowser::ServerListModel::kPageSize;
  for (int row = 0; row < rows; ++row) {
    const int index = list.entryIndexAt(row);
    if (index < 0)
      break;
    const ServerEntry *entry = query.entry(index);
    if (!entry)
      continue;
    mixValue(hash, list.keyAt(row));
    mixText(hash, entry->name);
    mixText(hash, entry->map);
    mixValue(hash, entry->players);
    mixValue(hash, entry->maxPlayers);
    mixValue(hash, entry->pingMs);
    mixValue(hash, entry->responded);
    mixValue(hash, entry->secure);
    mixValue(hash, entry->password);
    mixValue(hash, entry->botPlayers);
  }
  for (int i = 0; i < query.playerCount(); ++i) {
    const auto *player = query.player(i);
    if (!player)
      continue;
    mixText(hash, player->name);
    mixValue(hash, player->score);
  }
  // T18b: the selected server's favorite state is read-through (Steam is
  // the one truth); it must move the signature so a change re-projects even
  // when the list did not move.
  if (Favorites) {
    const ServerEntry *selected = list.selectedEntry(query);
    if (selected) {
      const auto key = services::serverbrowser::favoriteKeyOf(*selected);
      mixValue(hash, key.ip);
      mixValue(hash, key.connPort);
      mixValue(hash, key.queryPort);
      mixValue(hash,
               Favorites->isFavorite(key.ip, key.connPort, key.queryPort));
    }
  }
  return hash;
}

void ServerBrowserPage::render() {
  Rml::ElementDocument *doc = document();
  if (!doc || !Query || !List)
    return;
  const auto &query = *Query;
  const auto &list = *List;

  // --- status line (the module bar's right half) --------------------------
  std::string status;
  switch (query.status()) {
  case QueryStatus::Idle:
    break;
  case QueryStatus::Querying:
    status = tokenValue("SB_Querying", query.count());
    break;
  case QueryStatus::Completed:
    status = query.noServersListed() || query.count() == 0
                 ? token("SB_NoServers")
                 : tokenValue("SB_Completed", query.count());
    break;
  case QueryStatus::Cancelled:
    status = tokenValue("SB_Cancelled", query.count());
    break;
  case QueryStatus::Failed:
    switch (query.failure()) {
    case services::serverbrowser::QueryFailure::Unavailable:
      status = token("SB_FailUnavailable");
      break;
    case services::serverbrowser::QueryFailure::Rejected:
      status = token("SB_FailRejected");
      break;
    case services::serverbrowser::QueryFailure::TimedOut:
      status = token("SB_FailTimedOut");
      break;
    case services::serverbrowser::QueryFailure::MasterNoResponse:
    default:
      status = token("SB_FailMaster");
      break;
    }
    break;
  }
  if (!RuleStatus.empty())
    status = RuleStatus; // the blacklist editor's own feedback wins
  else if (list.blockedOnly())
    status.clear(); // the list query's count is noise in the rules view
  setHtml(doc, "sb-status", framework::TextUtil::escapeRml(status));
  // The module bar's left half: the visible row count of the active view
  // (the blacklist view counts blocked rules, not servers).
  setHtml(doc, "sb-count",
          framework::TextUtil::escapeRml(
              list.blockedOnly()
                  ? tokenValue("SB_BlacklistCount", list.rowCount())
                  : tokenValue("SB_ServerCount", list.rowCount())));

  // --- tabs ---------------------------------------------------------------
  for (auto Kind : {QueryKind::Internet, QueryKind::Lan, QueryKind::Favorites,
                    QueryKind::History, QueryKind::Friends}) {
    if (Rml::Element *tab = doc->GetElementById(tabActionOf(Kind)))
      tab->SetClass("sb-active", Kind == query.kind() && !list.blockedOnly());
  }
  if (Rml::Element *tab = doc->GetElementById("sb-tab-blacklist"))
    tab->SetClass("sb-active", list.blockedOnly());

  // --- the action bar -----------------------------------------------------
  // Refresh doubles as Stop while a list query is live: the shipped
  // browser's own Refresh/Stop discipline keeps cancellation reachable
  // without adding a fifth button.
  if (Rml::Element *refresh = doc->GetElementById("sb-refresh")) {
    const bool live = query.status() == QueryStatus::Querying;
    refresh->SetAttribute("data-action", live ? kActionStop : kActionRefresh);
    refresh->SetInnerRML(
        framework::TextUtil::escapeRml(token(live ? "SB_Stop" : "SB_Refresh")));
    setDisabled(doc, "sb-refresh", false);
  }
  // 自动重试 is a toggle: the lit state is the player's own switch.
  if (Rml::Element *toggle = doc->GetElementById("sb-auto-retry"))
    toggle->SetClass("sb-toggled", AutoRetryJoin);

  // --- sort markers -------------------------------------------------------
  const auto mark = [&](const char *id,
                        services::serverbrowser::ServerSort column) {
    if (Rml::Element *node = doc->GetElementById(id)) {
      const bool active = list.sortColumn() == column;
      node->SetClass("sb-sort-asc", active && list.sortAscending());
      node->SetClass("sb-sort-desc", active && !list.sortAscending());
    }
  };
  mark("sb-col-name", services::serverbrowser::ServerSort::Name);
  mark("sb-col-map", services::serverbrowser::ServerSort::Map);
  mark("sb-col-players", services::serverbrowser::ServerSort::Players);
  mark("sb-col-ping", services::serverbrowser::ServerSort::Ping);

  // --- rows (the bounded page window) -------------------------------------
  const int selectedRow = list.selectedRow();
  std::string rows;
  for (int row = 0; row < services::serverbrowser::ServerListModel::kPageSize;
       ++row) {
    const int index = list.entryIndexAt(row);
    if (index < 0)
      break;
    const ServerEntry *entry = query.entry(index);
    if (!entry)
      continue;
    rows += "<div class=\"sb-row";
    if (row == selectedRow)
      rows += " sb-selected";
    rows += "\" data-action=\"";
    rows += kActionRow;
    rows += "\" data-row=\"";
    rows += std::to_string(row);
    rows += "\"><span class=\"c-vac\">";
    if (entry->responded && entry->password)
      rows +=
          "<img class=\"server-icon\" src=\"../img/server-lock.png\" title=\"" +
          framework::TextUtil::escapeRmlAttribute(token("SB_PasswordYes")) +
          "\"/>";
    if (entry->responded && entry->secure)
      rows +=
          "<img class=\"server-icon\" src=\"../img/server-vac.png\" title=\"" +
          framework::TextUtil::escapeRmlAttribute(token("SB_SecureYes")) +
          "\"/>";
    rows += "</span><span class=\"c-name\">";
    rows += framework::TextUtil::escapeRml(entry->name);
    rows += "</span><span class=\"c-map\">";
    rows += framework::TextUtil::escapeRml(entry->map);
    rows += "</span><span class=\"c-players\">";
    rows += playersCell(*entry);
    rows += "</span><span class=\"c-bots\">";
    rows += botsCell(*entry);
    rows += "</span><span class=\"c-ping\">";
    rows += pingCell(*entry);
    rows += "</span></div>";
  }
  if (rows.empty())
    rows = "<p class=\"sb-empty\">" +
           framework::TextUtil::escapeRml(token("SB_EmptyList")) + "</p>";
  setHtml(doc, "sb-list", rows);

  // --- paging -------------------------------------------------------------
  setDisabled(doc, "sb-page-prev", list.page() <= 0);
  setDisabled(doc, "sb-page-next", list.page() >= list.pageCount() - 1);
  char pageLabel[48];
  _snprintf_s(pageLabel, sizeof(pageLabel), _TRUNCATE, "%d / %d",
              list.page() + 1, list.pageCount());
  setHtml(doc, "sb-page-label", framework::TextUtil::escapeRml(pageLabel));

  // --- the detail window's content (the CURRENT single slot only) ---------
  const ServerEntry *selected = list.selectedEntry(query);
  if (!selected) {
    setHtml(doc, "sb-detail-info",
            "<p class=\"sb-hint\">" +
                framework::TextUtil::escapeRml(token("SB_DetailHint")) +
                "</p>");
    setHtml(doc, "sb-detail-players", "");
    setDisabled(doc, "sb-connect", true);
  } else {
    std::string info = "<div id=\"sb-detail-name\">";
    info += framework::TextUtil::escapeRml(selected->name);
    info += "</div>";
    info += detailLine("SB_Address", addressCell(*selected));
    info += detailLine("SB_ColumnMap", selected->map);
    info += detailLine("SB_ColumnPlayers", playersCell(*selected));
    info += detailLine("SB_ColumnBots", botsCell(*selected));
    info += detailLine("SB_ColumnPing", pingCell(*selected));
    if (!selected->gameDir.empty())
      info += detailLine("SB_Game", selected->gameDir);
    std::string security = token("SB_SecureYes");
    if (selected->password)
      security = token("SB_PasswordYes") + " | " + security;
    info += detailLine("SB_Flags", security);
    setHtml(doc, "sb-detail-info", info);
    setDisabled(doc, "sb-connect", list.blockedOnly() || !selected->responded);

    std::string players;
    if (query.detailKind() == services::serverbrowser::DetailKind::Players &&
        query.detailStatus() == QueryStatus::Querying)
      players = "<p class=\"sb-hint\">" +
                framework::TextUtil::escapeRml(token("SB_DetailQuerying")) +
                "</p>";
    else if (query.detailKind() ==
                 services::serverbrowser::DetailKind::Players &&
             query.detailStatus() == QueryStatus::Failed)
      players = "<p class=\"sb-hint\">" +
                framework::TextUtil::escapeRml(token("SB_DetailFailed")) +
                "</p>";
    else if (query.detailKind() ==
                 services::serverbrowser::DetailKind::Players &&
             query.detailStatus() == QueryStatus::Completed) {
      if (query.playerCount() == 0)
        players = "<p class=\"sb-hint\">" +
                  framework::TextUtil::escapeRml(token("SB_NoPlayers")) +
                  "</p>";
      else
        players = "<div class=\"sb-player-head\"><span class=\"dp-name\">" +
                  framework::TextUtil::escapeRml(token("SB_PlayerName")) +
                  "</span><span class=\"dp-score\">" +
                  framework::TextUtil::escapeRml(token("SB_Score")) +
                  "</span><span class=\"dp-time\">" +
                  framework::TextUtil::escapeRml(token("SB_Time")) +
                  "</span></div>";
      for (int i = 0; i < query.playerCount(); ++i) {
        const auto *player = query.player(i);
        if (!player)
          continue;
        players += "<div class=\"sb-player\"><span class=\"dp-name\">";
        players += framework::TextUtil::escapeRml(player->name);
        players += "</span><span class=\"dp-score\">";
        players += std::to_string(player->score);
        players += "</span><span class=\"dp-time\">";
        players += playTime(static_cast<int>(player->seconds));
        players += "</span></div>";
      }
    }
    setHtml(doc, "sb-detail-players", players);
  }

  renderControls();
}

// The sub-state projection: the blacklist view, the filter column's values,
// the detail window's visibility and the row menu. The handlers below can
// call it directly for immediate feedback (its own writes never re-enter).
void ServerBrowserPage::renderControls() {
  Rml::ElementDocument *doc = document();
  if (!doc || !List || InControls)
    return;
  InControls = true;
  const auto Node = [&](const char *id) { return doc->GetElementById(id); };
  const bool blocked = List->blockedOnly();

  if (auto *module = Node("sb-list-module"))
    module->SetClass("sb-blacklist-mode", blocked);
  if (auto *panel = Node("sb-blacklist-panel"))
    panel->SetClass("sb-open", blocked);
  if (auto *panel = Node("sb-filter-module"))
    panel->SetClass("sb-hidden", blocked);

  // The filter column's projection (the model is the one truth; a focused
  // text control keeps what the user is typing until the change commits).
  const auto &filter = List->filter();
  if (auto *name = rmlui_dynamic_cast<Rml::ElementFormControl *>(
          Node("sb-filter-name"))) {
    if (!name->IsPseudoClassSet("focus") &&
        name->GetValue() != filter.Name.c_str())
      name->SetValue(filter.Name.c_str());
  }
  if (auto *map = rmlui_dynamic_cast<Rml::ElementFormControl *>(
          Node("sb-filter-map"))) {
    if (!map->IsPseudoClassSet("focus") &&
        map->GetValue() != filter.Map.c_str())
      map->SetValue(filter.Map.c_str());
  }
  syncSelect(doc, "sb-filter-ping", std::to_string(filter.MaxPing));
  syncSelect(doc, "sb-filter-empty", filter.hideEmpty ? "1" : "0");
  syncSelect(doc, "sb-filter-full", filter.hideFull ? "1" : "0");
  syncSelect(doc, "sb-filter-secure", filter.secureOnly ? "1" : "0");
  syncSelect(doc, "sb-filter-password", filter.NoPassword ? "1" : "0");

  // The detail window: a modal subpage. Its content is projected by
  // render(); this only moves the visibility.
  if (auto *overlay = Node("sb-detail-overlay"))
    overlay->SetClass("sb-open", DetailsOpen);

  // The row menu: the favorite entry reads the store through (Steam is the
  // one truth), and an unavailable store leaves the entry inert.
  if (auto *menu = Node("sb-context-menu"))
    menu->SetClass("sb-open", MenuOpen);
  const bool favoriteAvailable = Favorites && Favorites->available();
  if (auto *entry = Node("sb-menu-favorite")) {
    bool isFavorite = false;
    if (Favorites && Query) {
      if (const ServerEntry *selected = List->selectedEntry(*Query)) {
        const auto key = services::serverbrowser::favoriteKeyOf(*selected);
        isFavorite = key.valid() &&
                     Favorites->isFavorite(key.ip, key.connPort, key.queryPort);
      }
    }
    entry->SetInnerRML(framework::TextUtil::escapeRml(
        token(isFavorite ? "SB_FavoriteRemove" : "SB_FavoriteAdd")));
    entry->SetClass("sb-disabled", !favoriteAvailable);
  }
  if (auto *entry = Node("sb-menu-join"))
    entry->SetClass("sb-disabled", List->blockedOnly());
  if (auto *entry = Node("sb-menu-block"))
    entry->SetClass("sb-disabled", List->blockedOnly());
  if (auto *entry = Node("sb-menu-block-ip"))
    entry->SetClass("sb-disabled", List->blockedOnly());

  // The blacklist editor.
  if (Blacklist && Node("sb-blacklist-rules")) {
    std::string Markup;
    for (const auto &Rule : Blacklist->rules()) {
      // The Source dialog's shape: the blocked server, its address and
      // the moment it was blocked. A rule with no recorded name stays
      // visibly unnamed instead of repeating its address as a label.
      Markup +=
          "<div class=\"sb-blacklist-rule\"><span class=\"bl-name\">" +
          framework::TextUtil::escapeRml(Rule.Name.empty() ? "-" : Rule.Name) +
          "</span><span class=\"bl-ip\">" +
          framework::TextUtil::escapeRml(Rule.address()) +
          "</span><span class=\"bl-time\">" +
          framework::TextUtil::escapeRml(blockedAtCell(Rule.BlockedAt)) +
          "</span><span class=\"bl-action\"><button "
          "data-action=\"sb-rule-remove\" data-address=\"" +
          Rule.address() + "\">" +
          framework::TextUtil::escapeRml(token("SB_Unblock")) +
          "</button></span></div>";
    }
    Node("sb-blacklist-rules")->SetInnerRML(Markup);
  }
  InControls = false;
}

// Opens the row's secondary menu at the click point, clamped into the
// viewport (the menu is 132dp wide and 4 x 20dp tall plus padding).
void ServerBrowserPage::openContextMenu(int x, int y) {
  Rml::ElementDocument *doc = document();
  if (!doc)
    return;
  Rml::Element *menu = doc->GetElementById("sb-context-menu");
  if (!menu)
    return;
  MenuOpen = true;
  const float ratio = doc->GetContext()
                          ? doc->GetContext()->GetDensityIndependentPixelRatio()
                          : 1.0f;
  const int width = static_cast<int>(132.0f * ratio) + 6;
  const int height = static_cast<int>((4 * 20 + 4 + 2) * ratio) + 6;
  const int viewportW = static_cast<int>(doc->GetClientWidth());
  const int viewportH = static_cast<int>(doc->GetClientHeight());
  int left = x;
  int top = y;
  if (viewportW > 0 && left + width > viewportW)
    left = x - width;
  if (viewportH > 0 && top + height > viewportH)
    top = y - height;
  if (left < 0)
    left = 0;
  if (top < 0)
    top = 0;
  char buffer[32];
  _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%dpx", left);
  menu->SetProperty("left", buffer);
  _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%dpx", top);
  menu->SetProperty("top", buffer);
  menu->SetClass("sb-open", true);
}

void ServerBrowserPage::closeContextMenu() {
  if (!MenuOpen)
    return;
  MenuOpen = false;
  Projected = false;
  renderControls();
}

void ServerBrowserPage::closeDetails() {
  if (!DetailsOpen)
    return;
  DetailsOpen = false;
  if (Query)
    Query->cancelDetail(); // the window is the only consumer of the slot
  Projected = false;
  renderControls();
}

void ServerBrowserPage::handleClick(Rml::Event &event) {
  Rml::Element *target = event.GetTargetElement();
  Rml::ElementDocument *doc = document();
  Rml::Element *menu = doc ? doc->GetElementById("sb-context-menu") : nullptr;
  const bool insideMenu = target && menu && insideNode(target, menu);
  if (!insideMenu)
    closeContextMenu(); // any click outside the menu dismisses it
  Rml::Element *entry = actionNode(target);
  const Rml::String action =
      entry ? entry->GetAttribute<Rml::String>("data-action", "") : "";
#ifndef NDEBUG
  abc::ConsolePrint(
      "[AbcUi] browser click action='%s' details=%d menu=%d insideMenu=%d\n",
      action.c_str(), DetailsOpen ? 1 : 0, MenuOpen ? 1 : 0,
      insideMenu ? 1 : 0);
#endif

  if (insideMenu && MenuOpen) {
    // The secondary menu entries: join/favorite travel to the module,
    // the blacklist mutations stay page-owned. A dimmed entry is inert.
    if (entry && !entry->IsClassSet("sb-disabled") && !action.empty()) {
      if (action == kActionConnect || action == kActionFavoriteToggle) {
        const ActionSink Action = Sink;
        if (Action)
          Action(action.c_str());
      } else if (action == "sb-block" || action == "sb-block-ip") {
        handleControls(event);
      }
    }
    closeContextMenu();
    return;
  }

  if (DetailsOpen) {
    // A modal subpage: only its own bottom row answers; every other click
    // inside the background is inert. 加入 joins the shown server through
    // the ordinary connect intent (the selection is the shown server),
    // 自动重试 arms the join auto-retry for the full-server case.
    if (action == "sb-detail-close") {
      closeDetails();
      refocusDefault();
    } else if (action == "sb-auto-retry") {
      AutoRetryJoin = !AutoRetryJoin;
      Projected = false;
    } else if (action == kActionConnect) {
      const ActionSink Action = Sink;
      if (Action)
        Action(action.c_str());
    }
    return;
  }
  if (handleControls(event))
    return;
  if (!entry)
    return;

  // Page-internal view state first: selection, sort, paging (no module trip;
  // the re-render is the next tick's - the DOM is never rebuilt from inside
  // its own event dispatch).
  if (action == kActionRow) {
    if (!List || !Query)
      return;
    const Rml::String rowText =
        entry->GetAttribute<Rml::String>("data-row", "-1");
    List->selectRow(std::atoi(rowText.c_str()));
    const ServerEntry *selected = List->selectedEntry(*Query);
    if (selected)
      Query->requestPlayers(*selected); // single-slot replace (T17 model)
    List->sync(*Query);
    DetailsOpen = selected != nullptr; // the left click opens the window
    Projected = false;
    return;
  }
  if (action == kActionSort) {
    if (!List)
      return;
    const Rml::String column =
        entry->GetAttribute<Rml::String>("data-column", "name");
    services::serverbrowser::ServerSort sort;
    if (column == "map")
      sort = services::serverbrowser::ServerSort::Map;
    else if (column == "players")
      sort = services::serverbrowser::ServerSort::Players;
    else if (column == "ping")
      sort = services::serverbrowser::ServerSort::Ping;
    else
      sort = services::serverbrowser::ServerSort::Name;
    if (List->sortColumn() == sort)
      List->setSort(sort, !List->sortAscending());
    else
      List->setSort(sort, true);
    Projected = false;
    return;
  }
  if (action == kActionPagePrev) {
    if (List && List->pagePrev())
      Projected = false;
    return;
  }
  if (action == kActionPageNext) {
    if (List && List->pageNext())
      Projected = false;
    return;
  }

  // Module intents (fixed names; the module owns the query lifecycle, the
  // filter truth and every engine/Steam mutation, the tick re-projects).
  if (action == kActionTabInternet || action == kActionTabLan ||
      action == kActionTabFavorites || action == kActionTabHistory ||
      action == "sb-tab-friends" || action == kActionRefresh ||
      action == kActionStop || action == kActionConnect ||
      action == kActionFavoriteToggle || action == kActionReturn ||
      action == "sb-filters-apply") {
    const ActionSink Action = Sink;
    if (Action)
      Action(action.c_str());
  }
}

// Right click on a row: select it (no detail request - it stays a view
// action) and open the secondary menu at the pointer.
void ServerBrowserPage::handleMouseDown(Rml::Event &event) {
  const int button = event.GetParameter<int>("button", 0);
#ifndef NDEBUG
  Rml::Element *target = event.GetTargetElement();
  abc::ConsolePrint(
      "[AbcUi] browser mousedown button=%d target=%s#%s details=%d menu=%d\n",
      button, target ? target->GetTagName().c_str() : "-",
      target ? target->GetId().c_str() : "-", DetailsOpen ? 1 : 0,
      MenuOpen ? 1 : 0);
#endif
  if (button != 1)
    return;
  if (DetailsOpen || !List || !Query || List->blockedOnly())
    return;
  Rml::Element *row = rowNode(event.GetTargetElement());
  if (!row)
    return;
  const Rml::String rowText = row->GetAttribute<Rml::String>("data-row", "-1");
  List->selectRow(std::atoi(rowText.c_str()));
  List->sync(*Query);
  openContextMenu(event.GetParameter<int>("mouse_x", 0),
                  event.GetParameter<int>("mouse_y", 0));
  if (document())
    if (Rml::Element *first = document()->GetElementById("sb-menu-join"))
      first->Focus();
  Projected = false;
}

void ServerBrowserPage::PageListener::ProcessEvent(Rml::Event &event) {
  if (event.GetType() == "change")
    Owner->handleChange(event);
  else if (event.GetType() == "mousedown")
    Owner->handleMouseDown(event);
  else
    Owner->handleClick(event);
}

void ServerBrowserPage::refocusDefault() {
  if (Rml::Element *fallback =
          document() ? document()->GetElementById("sb-refresh") : nullptr)
    fallback->Focus();
}

} // namespace panels::serverbrowser
