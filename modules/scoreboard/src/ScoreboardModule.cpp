// abcui.scoreboard - the +showscores table. Moved from
// src/panels/scoreboard (old CScorePanel, ABCEnchance sources
// src/hud/vgui/scoreboard.*): 1397 lines of vgui SectionedListPanel machinery
// become one table rebuilt per data version / 0.5s while shown.
//
// Interaction: +showscores shows the table (the module reports the held state
// and visibility to the framework's score seat), MOUSE2 while visible unlocks
// the cursor through that seat, right-click on a row opens the context menu
// (profile / copy actions / voice mute). The seat owns the router token, so
// this module never touches the input router directly.
//
// D-17 notes kept from the built-in copy:
// - full table rebuild per data version / 0.5s (visible only).
// - the killer-highlight background needed a numeric DeathMsg feed the
//   text-based killfeed does not carry; not ported.
// - sorting drops the "frags+deaths+ping == 0" pre-filter.
// - avatars resolve through the framework avatar service ("avatar://<id>",
//   the bundled default until steam delivers).
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};

constexpr const char *kPanelName = "scoreboard";
constexpr const char *kDocumentPath =
    "mod://abcui.scoreboard/hud/rml/scoreboard.rml";

// Old ctor defaults (persisted contract - never rename).
constexpr const char *kShowAvatars = "hud_scoreboard_showavatars";
constexpr const char *kShowLoss = "hud_scoreboard_showloss";
constexpr const char *kShowSteamId = "hud_scoreboard_showsteamid";
constexpr const char *kShowSteamIdType = "hud_scoreboard_showsteamidtype";
constexpr const char *kShowRealName = "hud_scoreboard_showrealname";
constexpr const char *kSize = "hud_scoreboard_size";
constexpr const char *kSpacingNormal = "hud_scoreboard_spacing_normal";
constexpr const char *kSpacingCompact = "hud_scoreboard_spacing_compact";

AbcUiCvarHandle ShowAvatars = nullptr;
AbcUiCvarHandle ShowLoss = nullptr;
AbcUiCvarHandle ShowSteamId = nullptr;
AbcUiCvarHandle ShowSteamIdType = nullptr;
AbcUiCvarHandle ShowRealName = nullptr;
AbcUiCvarHandle Size = nullptr;
AbcUiCvarHandle SpacingNormal = nullptr;
AbcUiCvarHandle SpacingCompact = nullptr;

// Context-menu intents (data-action values).
enum class Action : int {
  kOpenProfile = 0,
  kCopyProfileUrl,
  kCopySteamName,
  kCopyGameName,
  kCopySteamId,
  kCopySteamId64,
  kMute,
};

std::string ServerName;
std::string NextMap;
int32_t TimeEndCount = 0;
double TimeEndBase = 0.0;
double LastRefresh = 0.0;
double ClientTimeNow = 0.0;
float AppliedRowHeight = -1.0f;
float CenteredHeight = -1.0f;
bool Visible = false;
bool MouseMode = false;
int MenuClient = 0;
long DataVersion = 0;
long AppliedVersion = -1;
std::string HeaderCache[5];
uint64_t ListenerDown = 0;
uint64_t ListenerClick = 0;

AbcUiDocument *panelDocument() {
  return Api->ui->panelDocument(kPanelName);
}

AbcUiElement *query(const char *id) {
  AbcUiDocument *doc = panelDocument();
  return doc ? Api->ui->querySelector(doc, id) : nullptr;
}

float cvarFloat(AbcUiCvarHandle cvar, float fallback) {
  return cvar ? Api->console->cvarValue(cvar, fallback) : fallback;
}

int cvarInt(AbcUiCvarHandle cvar, int fallback) {
  return cvar ? static_cast<int>(
                    Api->console->cvarValue(cvar, static_cast<float>(fallback)))
              : fallback;
}

void setProperty(AbcUiElement *element, const char *name, const char *value) {
  if (element)
    Api->ui->elementSetProperty(element, name, value);
}

void setVisible(bool visible) {
  if (visible == Visible)
    return;
  Visible = visible;
  Api->score->setVisible(visible ? 1 : 0);
  AbcUiDocument *doc = panelDocument();
  if (doc) {
    if (visible)
      Api->ui->documentShow(doc);
    else
      Api->ui->documentHide(doc);
  }
  if (!visible)
    Api->score->setPointerMode(0);
  if (visible) {
    DataVersion++;
    LastRefresh = -1.0;
  }
}

std::string escapeText(const std::string &text) {
  if (text.empty())
    return text;
  const int needed = Api->ui->escapeRml(text.c_str(), nullptr, 0);
  if (needed <= 0)
    return std::string();
  std::string out(static_cast<size_t>(needed) + 1, '\0');
  Api->ui->escapeRml(text.c_str(), out.data(),
                     static_cast<uint32_t>(out.size()));
  out.resize(static_cast<size_t>(needed));
  return out;
}

std::string localized(const char *token, const char *fallback) {
  char out[512];
  return Api->ui->translate(token, out, sizeof(out)) ? std::string(out)
                                                     : std::string(fallback);
}

// Old SectionedListPanel GetFgColorEx COLOR_PERCENT/COLOR_PING: hue rides the
// value (0 -> red, 90 -> green), saturation/value pinned.
std::string hueColor(float hue) {
  const float h = std::fmod(std::max(0.0f, std::min(90.0f, hue)) / 60.0f, 6.0f);
  const int sector = static_cast<int>(h);
  const float fraction = h - sector;
  float r = 0.0f, g = 0.0f, b = 0.0f;
  // HSV with S = 0.85, V = 0.95 (the scheme's fixed pair).
  const float s = 0.85f, v = 0.95f;
  const float p = v * (1.0f - s);
  const float q = v * (1.0f - s * fraction);
  const float t = v * (1.0f - s * (1.0f - fraction));
  switch (sector) {
  case 0:
    r = v, g = t, b = p;
    break;
  case 1:
    r = q, g = v, b = p;
    break;
  case 2:
    r = p, g = v, b = t;
    break;
  case 3:
    r = p, g = q, b = v;
    break;
  case 4:
    r = t, g = p, b = v;
    break;
  default:
    r = v, g = p, b = q;
    break;
  }
  char color[64];
  Api->ui->formatRgba(static_cast<int>(r * 255.0f),
                      static_cast<int>(g * 255.0f),
                      static_cast<int>(b * 255.0f), 255, color, sizeof(color));
  return color;
}

// Old COLOR_PERCENT cell: |value|/100*90 hue.
std::string percentColor(float value) {
  return hueColor(std::fabs(value) / 100.0f * 90.0f);
}

// Old COLOR_PING cell: 90 - |sum|/255*90 (fast ping keeps the green end).
std::string pingColor(int ping, int loss) {
  const float value = static_cast<float>(ping + loss);
  return hueColor(90.0f -
                  std::clamp(std::fabs(value) / 255.0f * 90.0f, 0.0f, 90.0f));
}

void appendFmt(std::string &out, const char *format, ...) {
  va_list args;
  va_start(args, format);
  const int needed = vsnprintf(nullptr, 0, format, args);
  va_end(args);
  if (needed <= 0)
    return;
  const size_t at = out.size();
  out.resize(at + static_cast<size_t>(needed));
  va_start(args, format);
  vsnprintf(out.data() + at, static_cast<size_t>(needed) + 1, format, args);
  va_end(args);
}

std::string strFmt(const char *format, ...) {
  std::string out;
  va_list args;
  va_start(args, format);
  const int needed = vsnprintf(nullptr, 0, format, args);
  va_end(args);
  if (needed <= 0)
    return out;
  out.resize(static_cast<size_t>(needed));
  va_start(args, format);
  vsnprintf(out.data(), static_cast<size_t>(needed) + 1, format, args);
  va_end(args);
  return out;
}

// ---- header ----

bool setHeaderRml(int slot, const char *id, const std::string &rml) {
  if (slot < 0 || slot >= 5 || HeaderCache[slot] == rml)
    return false;
  HeaderCache[slot] = rml;
  if (AbcUiElement *element = query(id))
    Api->ui->elementSetInnerRML(element, rml.c_str());
  return true;
}

void updateHeader() {
  setHeaderRml(0, "server-name", escapeText(ServerName));
  // Map name: the level path minus directory and extension.
  char levelName[256];
  if (Api->data->levelName(levelName, sizeof(levelName)) > 0) {
    std::string path = levelName;
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos)
      path = path.substr(slash + 1);
    const size_t dot = path.find_last_of('.');
    if (dot != std::string::npos)
      path = path.substr(0, dot);
    setHeaderRml(1, "map-name", escapeText(path));
  }
  int32_t playerCount = 0;
  for (int index = 1; index <= ABCUI_MAX_PLAYERS; index++) {
    AbcUiPlayerInfo info{};
    info.structSize = sizeof(info);
    if (Api->players->get(index, &info) && info.valid)
      playerCount++;
  }
  setHeaderRml(2, "player-count",
               strFmt("%d/%d", playerCount, ABCUI_MAX_PLAYERS));
  if (!NextMap.empty())
    setHeaderRml(3, "next-map", escapeText(NextMap));
  if (TimeEndCount > 0) {
    const double remaining =
        static_cast<double>(TimeEndCount) - (ClientTimeNow - TimeEndBase);
    const int seconds = static_cast<int>(std::max(0.0, remaining));
    setHeaderRml(4, "time-end",
                 strFmt("%02d:%02d", seconds / 60, seconds % 60));
  }
}

// ---- table ----

struct RowRef {
  AbcUiPlayerInfo info;
};

const char *donorIcon(int32_t donor) {
  switch (donor) {
  case 1:
    return "donor_1";
  case 2:
    return "donor_2";
  case 3:
    return "donor_3";
  case 4:
    return "donor_4";
  case 5:
    return "donor_5";
  case 6:
    return "donor_6";
  default:
    return "";
  }
}

std::string iconUri(const char *name) {
  return std::string("mod://abcui.scoreboard/hud/img/") + name + ".png";
}

void rebuildTable() {
  AbcUiElement *table = query("table");
  if (!table)
    return;
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;

  // Old per-row update(): re-read the engine mirror (identity + ping/loss)
  // before composing the table.
  Api->players->refresh(1);

  // Group connected players by team.
  std::map<int, std::vector<RowRef>> sections;
  for (int index = 1; index <= ABCUI_MAX_PLAYERS; index++) {
    AbcUiPlayerInfo info{};
    info.structSize = sizeof(info);
    if (!Api->players->get(index, &info) || !info.valid)
      continue;
    sections[info.team].push_back({info});
  }

  struct TeamScore {
    int frags = 0;
    int deaths = 0;
  };
  std::map<int, TeamScore> teamScores;
  for (auto &[team, rows] : sections) {
    for (RowRef &row : rows) {
      teamScores[team].frags += static_cast<int>(row.info.frags);
      teamScores[team].deaths += row.info.deaths;
    }
    std::sort(rows.begin(), rows.end(), [](const RowRef &a, const RowRef &b) {
      if (a.info.frags != b.info.frags)
        return a.info.frags > b.info.frags;
      if (a.info.deaths != b.info.deaths)
        return a.info.deaths < b.info.deaths;
      return a.info.index < b.info.index;
    });
  }
  std::vector<int> order;
  for (const auto &[team, rows] : sections) {
    (void)rows;
    order.push_back(team);
  }
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    const TeamScore &sa = teamScores[a];
    const TeamScore &sb = teamScores[b];
    if (sa.frags != sb.frags)
      return sa.frags > sb.frags;
    if (sa.deaths != sb.deaths)
      return sa.deaths < sb.deaths;
    return a > b;
  });

  const bool showSteamId = cvarInt(ShowSteamId, 1) > 0;
  const bool showLoss = cvarInt(ShowLoss, 1) > 0;
  const bool showRealName = cvarInt(ShowRealName, 1) > 0;
  const bool showAvatars = cvarInt(ShowAvatars, 1) > 0;

  // Inline sizes (the built-in panel post-processed the rebuilt DOM; the
  // module composes the same numbers directly).
  const float density = Api->ui->scale();
  const float rowHeight =
      AppliedRowHeight > 0.0f ? AppliedRowHeight : 28.0f * density;
  const float avatarSize =
      std::max(1.0f, std::min(28.0f * density, AppliedRowHeight - 2.0f));
  const float donorSize =
      std::max(1.0f, std::min(18.0f * density, AppliedRowHeight - 2.0f));

  std::string rml;
  rml.reserve(8192);

  const auto headerRow = [&]() {
    rml += "<div class='row header'>";
    rml += "<div class='cell c-avatar'></div><div class='cell c-donor'></div>";
    rml += "<div class='cell c-name'>" +
           escapeText(localized("Scores_PlayerName", "Name")) + "</div>";
    if (showSteamId)
      rml += "<div class='cell c-steamid'>" +
             escapeText(localized("Scores_SteamID", "SteamID")) + "</div>";
    rml += "<div class='cell c-health'>" +
           escapeText(localized("Scores_PlayerHealth", "HP")) + "</div>";
    rml += "<div class='cell c-armor'>" +
           escapeText(localized("Scores_PlayerArmor", "AP")) + "</div>";
    rml += "<div class='cell c-frags'>" +
           escapeText(localized("Scores_PlayerScore", "Frags")) + "</div>";
    rml += "<div class='cell c-deaths'>" +
           escapeText(localized("Scores_PlayerDeath", "Death")) + "</div>";
    rml += "<div class='cell c-ping'>" +
           escapeText(localized(showLoss ? "Scores_lPingLoss" : "Scores_Ping",
                                showLoss ? "Ping/Loss" : "Ping")) +
           "</div></div>";
  };
  headerRow();

  const std::string na = localized("Scores_NA", "N/A");
  const std::string specTag = localized("Scores_PlayerSpec", "(Spectator)");
  const std::string deadTag = localized("Scores_PlayerHealthDead", "DEAD");

  int connectedTotal = 0;
  for (int index = 1; index <= ABCUI_MAX_PLAYERS; index++) {
    AbcUiPlayerInfo info{};
    info.structSize = sizeof(info);
    if (Api->players->get(index, &info) && info.valid)
      connectedTotal++;
  }
  const bool singleTeamNone = order.size() == 1 && order[0] == 0;
  const int localIndex = [&]() {
    AbcUiPlayerState player{};
    player.structSize = sizeof(player);
    Api->data->playerState(&player);
    return player.index;
  }();

  const auto appendAvatar = [&](const AbcUiPlayerInfo &info, std::string &out) {
    if (!showAvatars) {
      out += "<div class='cell c-avatar'></div>";
      return;
    }
    appendFmt(out,
              "<div class='cell c-avatar'><div class='avatar-frame' "
              "style='width:%.0fpx; height:%.0fpx;'>"
              "<img class='avatar' style='width:100%%; height:100%%;' "
              "src='avatar://%llu?gen=%d'/>",
              static_cast<double>(avatarSize), static_cast<double>(avatarSize),
              static_cast<unsigned long long>(info.steamId64),
              Api->players->avatarGeneration(info.steamId64));
    if (info.admin == 1)
      appendFmt(out,
                "<img class='admin' style='width:100%%; height:100%%;' "
                "src='%s'/>",
                iconUri("admin_icon32").c_str());
    else if (info.admin == 2)
      appendFmt(out,
                "<img class='admin' style='width:100%%; height:100%%;' "
                "src='%s'/>",
                iconUri("serverowner_icon32").c_str());
    if (Api->voice->available() && Api->voice->isBlocked(info.index))
      appendFmt(out,
                "<img class='mute' style='width:100%%; height:100%%;' "
                "src='%s'/>",
                iconUri("mute_icon32").c_str());
    out += "</div></div>";
  };

  for (int team : order) {
    const std::vector<RowRef> &rows = sections[team];
    if (!singleTeamNone) {
      char tokenBuffer[64] = {};
      std::string teamName;
      if (Api->players->teamNameToken(team, tokenBuffer, sizeof(tokenBuffer)) >
          0)
        teamName = localized(tokenBuffer, tokenBuffer);
      appendFmt(rml,
                "<div class='row section'>"
                "<div class='cell c-avatar'></div>"
                "<div class='cell c-donor'></div>"
                "<div class='cell c-name'>%s (%d/%d)</div>",
                escapeText(teamName).c_str(), static_cast<int>(rows.size()),
                connectedTotal);
      if (showSteamId)
        rml += "<div class='cell c-steamid'></div>";
      rml += "<div class='cell c-health'></div><div class='cell "
             "c-armor'></div>";
      appendFmt(rml, "<div class='cell c-frags'>%d</div>",
                teamScores[team].frags);
      appendFmt(rml, "<div class='cell c-deaths'>%d</div>",
                teamScores[team].deaths);
      rml += "<div class='cell c-ping'></div></div>";
    }

    for (const RowRef &row : rows) {
      const AbcUiPlayerInfo &info = row.info;
      const bool isLocal = info.index == localIndex;
      std::string rowOpen = "<div class='row";
      if (isLocal)
        rowOpen += " thisplayer";
      float tint[3];
      std::string style;
      if (Api->players->clientColor(info.index, tint)) {
        const int r = static_cast<int>(tint[0] * 255.0f);
        const int g = static_cast<int>(tint[1] * 255.0f);
        const int b = static_cast<int>(tint[2] * 255.0f);
        style = strFmt("color: rgba(%d, %d, %d, 255);", r, g, b);
        if (!isLocal)
          style += strFmt(" background-color: rgba(%d, %d, %d, 80);", r, g, b);
      }
      appendFmt(rowOpen, "' style='%s height: %.1fpx;' data-client='%d'>",
                style.c_str(), static_cast<double>(rowHeight), info.index);
      rml += rowOpen;

      appendAvatar(info, rml);
      const char *donor = donorIcon(info.donor);
      if (donor[0])
        appendFmt(rml,
                  "<div class='cell c-donor'><img style='width:%.0fpx; "
                  "height:%.0fpx;' src='%s'/></div>",
                  static_cast<double>(donorSize),
                  static_cast<double>(donorSize), iconUri(donor).c_str());
      else
        rml += "<div class='cell c-donor'></div>";

      std::string name = info.name;
      if (showRealName && info.realName[0]) {
        name += " (";
        name += info.realName;
        name += ")";
      }
      appendFmt(rml, "<div class='cell c-name'>%s</div>",
                escapeText(name).c_str());

      if (showSteamId) {
        std::string steam;
        if (info.steamId64 != 0) {
          if (cvarInt(ShowSteamIdType, 1) > 0) {
            steam = std::to_string(info.steamId64);
          } else {
            const std::uint32_t accountId =
                static_cast<std::uint32_t>(info.steamId64 & 0xFFFFFFFFull);
            steam = strFmt("STEAM_0:%d:%d", accountId & 1, accountId >> 1);
          }
        }
        appendFmt(rml, "<div class='cell c-steamid'>%s</div>",
                  escapeText(steam).c_str());
      }

      if (info.spectate) {
        rml += "<div class='cell c-health' style='color: rgba(128, 128, 128, "
               "255);'>" +
               escapeText(specTag) + "</div>";
        rml += "<div class='cell c-armor' style='color: rgba(128, 128, 128, "
               "255);'>" +
               escapeText(na) + "</div>";
      } else {
        if (info.health < 0)
          appendFmt(rml, "<div class='cell c-health'>%s</div>",
                    escapeText(deadTag).c_str());
        else
          appendFmt(
              rml, "<div class='cell c-health' style='color: %s;'>%d</div>",
              percentColor(info.health).c_str(), static_cast<int>(info.health));
        if (info.armor < 0)
          appendFmt(rml, "<div class='cell c-armor'>%s</div>",
                    escapeText(na).c_str());
        else
          appendFmt(
              rml, "<div class='cell c-armor' style='color: %s;'>%d</div>",
              percentColor(info.armor).c_str(), static_cast<int>(info.armor));
      }

      appendFmt(rml,
                "<div class='cell c-frags'>%d</div>"
                "<div class='cell c-deaths'>%d</div>",
                static_cast<int>(info.frags), info.deaths);
      if (showLoss)
        appendFmt(
            rml, "<div class='cell c-ping' style='color: %s;'>%d/%d</div>",
            pingColor(info.ping, info.loss).c_str(), info.ping, info.loss);
      else
        appendFmt(rml, "<div class='cell c-ping' style='color: %s;'>%d</div>",
                  pingColor(info.ping, 0).c_str(), info.ping);
      rml += "</div>";
    }
  }

  Api->ui->elementSetInnerRML(table, rml.c_str());
  Api->ui->elementSetClass(table, "no-steamid", showSteamId ? 0 : 1);
  char lineHeight[32];
  std::snprintf(lineHeight, sizeof(lineHeight), "%gpx",
                static_cast<double>(rowHeight));
  setProperty(table, "line-height", lineHeight);
}

// ---- context menu ----

void closeContextMenu() {
  MenuClient = 0;
  setProperty(query("menu"), "display", "none");
}

void menuAction(Action action) {
  AbcUiPlayerInfo info{};
  info.structSize = sizeof(info);
  if (!Api->players->get(MenuClient, &info) || !info.valid)
    return;
  const std::string profileUrl =
      strFmt("http://steamcommunity.com/profiles/%llu",
             static_cast<unsigned long long>(info.steamId64));
  switch (action) {
  case Action::kOpenProfile:
    Api->shell->openUrl(profileUrl.c_str());
    break;
  case Action::kCopyProfileUrl:
    Api->shell->copyText(profileUrl.c_str());
    break;
  case Action::kCopySteamName:
    Api->shell->copyText(info.realName);
    break;
  case Action::kCopyGameName:
    Api->shell->copyText(info.name);
    break;
  case Action::kCopySteamId: {
    const std::uint32_t accountId =
        static_cast<std::uint32_t>(info.steamId64 & 0xFFFFFFFFull);
    Api->shell->copyText(
        strFmt("STEAM_0:%d:%d", accountId & 1, accountId >> 1).c_str());
    break;
  }
  case Action::kCopySteamId64:
    Api->shell->copyText(std::to_string(info.steamId64).c_str());
    break;
  case Action::kMute: {
    const bool blocked = Api->voice->isBlocked(MenuClient) != 0;
    Api->voice->setBlocked(MenuClient, blocked ? 0 : 1);
    DataVersion++;
    break;
  }
  }
}

void openContextMenu(int clientIndex, float mouseX, float mouseY) {
  AbcUiPlayerInfo info{};
  info.structSize = sizeof(info);
  if (!Api->players->get(clientIndex, &info) || !info.valid)
    return;
  AbcUiPlayerState player{};
  player.structSize = sizeof(player);
  Api->data->playerState(&player);
  MenuClient = clientIndex;
  AbcUiElement *menu = query("menu");
  if (!menu)
    return;

  const bool hasSteamId = info.steamId64 != 0;
  const bool mutablePlayer =
      hasSteamId && clientIndex != player.index && Api->voice->available() != 0;
  struct Entry {
    const char *id;
    Action action;
    const char *token;
    const char *fallback;
  };
  const Entry entries[] = {
      {"menu-profile", Action::kOpenProfile, "Scores_MenuSteamProfile",
       "Open Steam Profile"},
      {"menu-copy-url", Action::kCopyProfileUrl, "Scores_MenuSteamURL",
       "Copy Profile URL"},
      {"menu-copy-name", Action::kCopySteamName, "Scores_MenuCopyName",
       "Copy Steam Name"},
      {"menu-copy-name-raw", Action::kCopyGameName, "Scores_MenuCopyNameRaw",
       "Copy Game Name"},
      {"menu-copy-steamid", Action::kCopySteamId, "Scores_MenuCopySteamID",
       "Copy SteamID"},
      {"menu-copy-steamid64", Action::kCopySteamId64,
       "Scores_MenuCopySteamID64", "Copy SteamID64"},
  };
  for (const Entry &entry : entries) {
    if (AbcUiElement *item = query(entry.id)) {
      const std::string text =
          escapeText(localized(entry.token, entry.fallback));
      Api->ui->elementSetInnerRML(item, text.c_str());
      Api->ui->elementSetAttribute(
          item, "data-action",
          std::to_string(static_cast<int>(entry.action)).c_str());
    }
  }
  setProperty(query("menu-profile"), "display", hasSteamId ? "block" : "none");
  setProperty(query("menu-copy-name"), "display",
              info.realName[0] ? "block" : "none");
  if (AbcUiElement *item = query("menu-mute")) {
    if (mutablePlayer) {
      // The label follows the backend's current truth - never the last click.
      const bool blocked = Api->voice->isBlocked(MenuClient) != 0;
      const std::string text = escapeText(
          localized(blocked ? "Scores_MenuUnmute" : "Scores_MenuMute",
                    blocked ? "Unmute Voice" : "Mute Voice"));
      Api->ui->elementSetInnerRML(item, text.c_str());
      Api->ui->elementSetAttribute(
          item, "data-action",
          std::to_string(static_cast<int>(Action::kMute)).c_str());
      setProperty(item, "display", "block");
    } else {
      setProperty(item, "display", "none");
    }
  }

  // Position at the pointer, clamped into the window; #menu sits inside
  // #box (a centered absolute container), so subtract the box offset.
  float x = mouseX;
  float y = mouseY;
  int width = 0;
  int height = 0;
  if (Api->ui->documentSize(panelDocument(), &width, &height)) {
    x = std::clamp(
        x, 0.0f,
        std::max(0.0f, static_cast<float>(width) - 180.0f * Api->ui->scale()));
    y = std::clamp(
        y, 0.0f,
        std::max(0.0f, static_cast<float>(height) - 100.0f * Api->ui->scale()));
  }
  char left[32];
  char top[32];
  std::snprintf(left, sizeof(left), "%gpx", static_cast<double>(x));
  std::snprintf(top, sizeof(top), "%gpx", static_cast<double>(y));
  setProperty(menu, "left", left);
  setProperty(menu, "top", top);
  setProperty(menu, "display", "block");
}

// ---- DOM listener (delegated rows + menu) ----

int32_t onBoxEvent(void *, AbcUiElement *target,
                   const AbcUiElementEvent *event) {
  if (!event || !event->type)
    return 0;
  const std::string type = event->type;
  if (type != "mousedown" && type != "click")
    return 0;

  bool overMenu = false;
  for (AbcUiElement *node = target; node; node = Api->ui->elementParent(node)) {
    if (std::strcmp(Api->ui->elementId(node), "menu") == 0)
      overMenu = true;
    const int action = Api->ui->elementAttributeInt(node, "data-action", -1);
    if (action >= 0 && type == "click" && event->button == 0) {
      menuAction(static_cast<Action>(action));
      closeContextMenu();
      return 1;
    }
    const int client = Api->ui->elementAttributeInt(node, "data-client", 0);
    if (client > 0 && type == "mousedown" && event->button == 1) {
      openContextMenu(client, event->mouseX, event->mouseY);
      return 1;
    }
  }
  // Clicks inside the open menu must not dismiss it before the click action
  // runs.
  if (type == "mousedown" && !overMenu)
    closeContextMenu();
  return 0;
}

// ---- event sinks ----

void onIntermission(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiBoolEvent *>(payload);
  setVisible(event->value != 0);
}

void onPointer(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiBoolEvent *>(payload);
  if (!event->value)
    closeContextMenu();
}

// ---- commands ----

int32_t onShowScores(void *, const void *) {
  if (!Api->score->intermission()) {
    setVisible(true);
    // Old Viewport m_bInScore: the button state the usercmd stream reports
    // (IN_SCORE) - the server streams ping/loss only to clients it believes
    // hold the scoreboard.
    Api->score->setHeld(1);
  }
  return 1;
}

int32_t onHideScores(void *, const void *) {
  if (!Api->score->intermission()) {
    setVisible(false);
    Api->score->setPointerMode(0);
  }
  Api->score->setHeld(0);
  return 1;
}

// ---- panel callbacks ----

void panelReset(void *) {
  setVisible(false);
  Api->score->setHeld(0);
  ServerName.clear();
  NextMap.clear();
  TimeEndCount = 0;
  MenuClient = 0;
  DataVersion++;
  closeContextMenu();
}

void panelOnData(void *, double frameTime) {
  ClientTimeNow += frameTime;

  // The seat owns pointer mode; a force-release leaves this mirror stale.
  if (MouseMode != (Api->score->pointerMode() != 0)) {
    MouseMode = Api->score->pointerMode() != 0;
    if (!MouseMode)
      closeContextMenu();
  }

  // Header data (server name / next map / time limit + level name).
  char name[256];
  char next[256];
  int32_t timeEnd = 0;
  Api->data->serverInfo(name, sizeof(name), next, sizeof(next), &timeEnd);
  if (ServerName != name) {
    ServerName = name;
    DataVersion++;
  }
  if (NextMap != next) {
    NextMap = next;
    DataVersion++;
  }
  if (TimeEndCount != timeEnd) {
    TimeEndCount = timeEnd;
    TimeEndBase = ClientTimeNow;
    DataVersion++;
  }

  if (!Visible) {
    Api->score->setHeld(0);
    return;
  }

  // Row height: hud_scoreboard_size (0 auto / 1 normal / 2 compact) with the
  // spacing cvars overriding the defaults.
  const float density = Api->ui->scale();
  int screenHeight = 0;
  int screenWidth = 0;
  Api->ui->documentSize(panelDocument(), &screenWidth, &screenHeight);
  (void)screenWidth;
  const auto spacing = [&](AbcUiCvarHandle cvar, float fallback) {
    const float value = cvarFloat(cvar, 0.0f);
    return std::isfinite(value) && value > 0.0f
               ? std::clamp(value, 8.0f, 128.0f)
               : fallback;
  };
  int playerCount = 0;
  for (int index = 1; index <= ABCUI_MAX_PLAYERS; index++) {
    AbcUiPlayerInfo info{};
    info.structSize = sizeof(info);
    if (Api->players->get(index, &info) && info.valid)
      playerCount++;
  }
  const int mode = cvarInt(Size, 0);
  const float normal = spacing(SpacingNormal, 28.0f * density);
  const float compactDefault = screenHeight < 600    ? 18.0f
                               : screenHeight < 720  ? 22.0f
                               : screenHeight < 800  ? 24.0f
                               : screenHeight < 1024 ? 26.0f
                                                     : 28.0f;
  const bool compact =
      mode == 2 || (mode == 0 && (playerCount + 2) * normal > 420.0f * density);
  const float height =
      compact ? spacing(SpacingCompact, compactDefault) : normal;
  if (height != AppliedRowHeight) {
    AppliedRowHeight = height;
    DataVersion++;
  }

  updateHeader();

  // Old OnThink: 0.5s full refresh cadence; version bumps refresh
  // immediately.
  if (DataVersion != AppliedVersion || ClientTimeNow - LastRefresh >= 0.5) {
    AppliedVersion = DataVersion;
    LastRefresh = ClientTimeNow;
    rebuildTable();
  }

  // Vertical centering: measure the laid-out box. The table carries a
  // min-height (300dp) and the header/padding add to it, so deriving the
  // content height from (rows + 1) * rowHeight left the box low and pushed
  // its bottom off screen for small tables. The rebuild above lands on the
  // layout in the same beat; a height change re-centers on the next one.
  if (AbcUiElement *box = query("box")) {
    float boxWidth = 0.0f;
    float boxHeight = 0.0f;
    if (Api->ui->elementOffsetSize(box, &boxWidth, &boxHeight) &&
        boxHeight > 0.0f && boxHeight != CenteredHeight) {
      CenteredHeight = boxHeight;
      if (screenHeight > 0) {
        const float top = std::max(
            0.0f, (static_cast<float>(screenHeight) - boxHeight) * 0.5f);
        char value[32];
        std::snprintf(value, sizeof(value), "%gpx", static_cast<double>(top));
        setProperty(box, "top", value);
      }
    }
  }
}

// ---- lifecycle ----

void onModuleLoad() {
  Panel.structSize = sizeof(Panel);
  Panel.name = kPanelName;
  Panel.documentPath = kDocumentPath;
  Panel.zIndex = 0;
  Panel.userdata = nullptr;
  Panel.onDocumentReady = nullptr;
  Panel.onData = &panelOnData;
  Panel.onRedraw = nullptr;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);

  AbcUiElement *box = query("box");
  if (box) {
    ListenerDown =
        Api->ui->elementAddListener(box, "mousedown", &onBoxEvent, nullptr);
    ListenerClick =
        Api->ui->elementAddListener(box, "click", &onBoxEvent, nullptr);
  }

  Api->events->subscribe(ABCUI_CHANNEL_HUD_INTERMISSION, 0, &onIntermission,
                         nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_SCORE_POINTER, 0, &onPointer,
                         nullptr);
  Api->commands->subscribeCommand("+showscores", 0, &onShowScores, nullptr);
  Api->commands->subscribeCommand("-showscores", 0, &onHideScores, nullptr);
}

void onModuleInit() {
  ShowAvatars = Api->console->registerCvar(kShowAvatars, "1", ABCUI_CVAR_VALUE);
  ShowLoss = Api->console->registerCvar(kShowLoss, "1", ABCUI_CVAR_VALUE);
  ShowSteamId = Api->console->registerCvar(kShowSteamId, "1", ABCUI_CVAR_VALUE);
  ShowSteamIdType =
      Api->console->registerCvar(kShowSteamIdType, "1", ABCUI_CVAR_VALUE);
  ShowRealName =
      Api->console->registerCvar(kShowRealName, "1", ABCUI_CVAR_VALUE);
  Size = Api->console->registerCvar(kSize, "0", ABCUI_CVAR_VALUE);
  SpacingNormal =
      Api->console->registerCvar(kSpacingNormal, "0", ABCUI_CVAR_VALUE);
  SpacingCompact =
      Api->console->registerCvar(kSpacingCompact, "0", ABCUI_CVAR_VALUE);
}

void onModuleShutdown() {
  // The document dies with the context: drop the listeners and the menu.
  if (ListenerDown || ListenerClick) {
    if (AbcUiElement *box = query("box")) {
      if (ListenerDown)
        Api->ui->elementRemoveListener(box, "mousedown", ListenerDown);
      if (ListenerClick)
        Api->ui->elementRemoveListener(box, "click", ListenerClick);
    }
    ListenerDown = 0;
    ListenerClick = 0;
  }
  MenuClient = 0;
  Visible = false;
  MouseMode = false;
  Api->score->setHeld(0);
  Api->score->setVisible(0);
  Api->score->setPointerMode(0);
}

void onModuleUnload() {
  Api = nullptr;
}

} // namespace

ABCUI_MODULE_EXPORT int AbcUiModule_Main(uint32_t abiVersion,
                                         const AbcUiModuleApi *api,
                                         AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api || !out)
    return 0;
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->panels ||
      !api->events || !api->ui || !api->console || !api->data ||
      !api->players || !api->score || !api->shell || !api->voice)
    return 0;
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->console->structSize < sizeof(AbcUiConsoleApi) ||
      api->data->structSize < sizeof(AbcUiDataApi) ||
      api->players->structSize < sizeof(AbcUiPlayersApi) ||
      api->score->structSize < sizeof(AbcUiScoreApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.scoreboard";
  out->version = "1.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
