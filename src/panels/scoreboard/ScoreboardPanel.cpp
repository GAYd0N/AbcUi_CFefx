// panels::Scoreboard implementation - see ScoreboardPanel.h.
// from: ABCEnchance/src/hud/vgui/scoreboard.cpp (GBK->UTF-8).
// D-17 notes on the deliberate deviations:
// - The vgui SectionedListPanel item-id bookkeeping becomes a full table
//   rebuild per data version / 0.5s (visible only) - 32 rows of innerRML is
//   well inside budget and removes the old add/remove-item state machine.
// - The killer-highlight background needed a numeric DeathMsg feed the new
//   text-based killfeed does not carry; not ported (low signal, logged).
// - Sorting drops the "frags+deaths+ping == 0" pre-filter (the old
//   not-yet-updated heuristic); zero-score rows simply sort to the bottom.
// - Avatar textures resolve through data::avatars ("avatar://<id>", the
//   bundled default until steam delivers; the animated-GIF path and the
//   friend overlay need the steamapi backend's Friends surface).
#include <metahook.h>

#include "ScoreboardPanel.h"
#include "../../data/HudState.h"
#include "../../framework/ColorUtil.h"
#include "../../framework/PanelManager.h"
#include "../../framework/ShellActions.h"
#include "../../framework/Theme.h"
#include "../../services/voice/VoiceStatus.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/SystemInterface.h>

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <format>
#include <map>
#include <vector>

#include <abc/CvarCmd.h>

#include <keydefs.h>

// Windows.h min/max macros fight std::min/std::max (old repo precedent).
#undef max
#undef min

#include "../../rmlui/texture/Avatars.h"
#include "../../data/NetworkMessages.h"
#include "../../data/PlayerResource.h"
#include "../../data/TeamResource.h"
#include "../../framework/Commands.h"
#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"
#include "../../hooks.h"
#include "../../rmlui/InputBridge.h"
#include "../../rmlui/RmlUiModule.h"
#include "../../rmlui/VGUI2ExtensionImport.h"

namespace panels {

namespace scoreboard {

// Old ctor defaults (persisted contract - never rename).
cvar_t *ShowAvatars = nullptr;
cvar_t *ShowLoss = nullptr;
cvar_t *ShowSteamId = nullptr;
cvar_t *ShowSteamIdType = nullptr;
cvar_t *ShowRealName = nullptr;
cvar_t *Size = nullptr;
cvar_t *SpacingNormal = nullptr;
cvar_t *SpacingCompact = nullptr;

void registerCvars() {
  ShowAvatars = abc::CREATE_CVAR("hud_scoreboard_showavatars", "1",
                                 abc::FCVAR_VALUE, nullptr);
  ShowLoss = abc::CREATE_CVAR("hud_scoreboard_showloss", "1", abc::FCVAR_VALUE,
                              nullptr);
  ShowSteamId = abc::CREATE_CVAR("hud_scoreboard_showsteamid", "1",
                                 abc::FCVAR_VALUE, nullptr);
  ShowSteamIdType = abc::CREATE_CVAR("hud_scoreboard_showsteamidtype", "1",
                                     abc::FCVAR_VALUE, nullptr);
  ShowRealName = abc::CREATE_CVAR("hud_scoreboard_showrealname", "1",
                                  abc::FCVAR_VALUE, nullptr);
  Size =
      abc::CREATE_CVAR("hud_scoreboard_size", "0", abc::FCVAR_VALUE, nullptr);
  SpacingNormal = abc::CREATE_CVAR("hud_scoreboard_spacing_normal", "0",
                                   abc::FCVAR_VALUE, nullptr);
  SpacingCompact = abc::CREATE_CVAR("hud_scoreboard_spacing_compact", "0",
                                    abc::FCVAR_VALUE, nullptr);
}

namespace {
ScoreboardPanel *instance() {
  return static_cast<ScoreboardPanel *>(
      framework::PanelManager::find("scoreboard"));
}
} // namespace

bool onKeyEvent(int down, int keynum) {
  ScoreboardPanel *panel = instance();
  return panel && panel->answerKey(down, keynum);
}

void setIntermission(bool intermission) {
  // Edge-triggered: HUD_Redraw calls this every frame with the current
  // flag; only the 0->1/1->0 transitions drive the panel (old
  // SetInterMission was a state change, not a per-frame vote).
  ScoreboardPanel *panel = instance();
  if (!panel || panel->Intermission == intermission)
    return;
  panel->Intermission = intermission;
  // Old SetInterMission -> ShowPanel(intermission > 0).
  panel->setVisible(intermission);
  // The pointer never auto-engages on the intermission board (old code kept
  // +/-showscores dead there; MOUSE2 stays live for players who want it).
}

bool inScore() {
  ScoreboardPanel *panel = instance();
  return panel && panel->InScore;
}

bool wantsWheel() {
  ScoreboardPanel *panel = instance();
  return panel && panel->isVisible() && panel->MouseMode;
}

void setPointerMode(bool enabled) {
  ScoreboardPanel *panel = instance();
  if (panel && (!enabled || panel->isVisible()))
    panel->setMouseMode(enabled);
}

} // namespace scoreboard

namespace {

constexpr int kMouse2 = K_MOUSE2;

std::string localized(const char *token, const char *fallback) {
  std::string out;
  return framework::Localization::translate(out, token) ? out : fallback;
}

// printf-composed RML fragments: server-derived strings (names, localized
// tags) are unbounded, so every cell composes into the target string - no
// fixed cell buffers (the P6 audit round traced a CRT "Buffer too small"
// fail-fast to exactly this class).
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

// Old SectionedListPanel GetFgColorEx COLOR_PERCENT/COLOR_PING: hue rides
// the value (0 -> red, full -> hue 90), saturation/value pinned.
std::string hueColor(float hue) {
  // HSV(h,1,1) -> rgb (hue in degrees 0..120 here).
  hue = std::clamp(hue, 0.0f, 90.0f);
  const float sector = hue / 60.0f;
  const float c = 1.0f;
  const float x = c * (1.0f - std::fabs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
  float r = 0, g = 0, b = 0;
  if (sector < 1.0f) {
    r = c;
    g = x;
  } else {
    r = x;
    g = c;
  }
  return framework::ColorUtil::formatRgba(static_cast<int>(r * 255.0f),
                                          static_cast<int>(g * 255.0f),
                                          static_cast<int>(b * 255.0f), 255);
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

// Old Viewport::GetPlayerColor (client dll per-player tint, alpha 255).
bool playerColor(int index, int &r, int &g, int &b) {
  if (!hooks::GetClientColor)
    return false;
  const float *tint = hooks::GetClientColor(index);
  if (!tint)
    return false;
  r = static_cast<int>(std::clamp(tint[0], 0.0f, 1.0f) * 255.0f);
  g = static_cast<int>(std::clamp(tint[1], 0.0f, 1.0f) * 255.0f);
  b = static_cast<int>(std::clamp(tint[2], 0.0f, 1.0f) * 255.0f);
  return true;
}

} // namespace

// Table mouse plumbing: row right-click opens the context menu, menu item
// clicks run their action, a click elsewhere closes it.
class ScoreboardPanel::TableListener final : public Rml::EventListener {
public:
  void ProcessEvent(Rml::Event &event) override {
    if (!Panel)
      return;
    const Rml::String type = event.GetType();
    if (type != "mousedown" && type != "click")
      return;
    const int button = event.GetParameter("button", 0);
    const Rml::Vector2f mouse(
        static_cast<float>(event.GetParameter("mouse_x", 0)),
        static_cast<float>(event.GetParameter("mouse_y", 0)));

    // Walk up from the event TARGET (bubble-phase GetCurrentElement is
    // the listening box itself, not the pressed row - RmlUi
    // EventDispatcher sets CurrentElement per listener attachment).
    bool overMenu = false;
    for (Rml::Element *node = event.GetTargetElement(); node;
         node = node->GetParentNode()) {
      if (node->GetId() == "menu")
        overMenu = true;
      if (node->HasAttribute("data-action")) {
        if (type == "click" && button == 0) {
          if (const Rml::Variant *value = node->GetAttribute("data-action")) {
            Panel->menuAction(static_cast<ScoreboardAction>(value->Get<int>()));
            Panel->closeContextMenu();
            event.StopPropagation();
            return;
          }
        }
      }
      if (node->HasAttribute("data-client")) {
        const Rml::Variant *value = node->GetAttribute("data-client");
        const int client = value ? value->Get<int>() : 0;
        if (type == "mousedown" && button == 1 && client > 0) {
          Panel->openContextMenu(client, mouse);
          event.StopPropagation();
          return;
        }
      }
    }
    // Clicks inside the open menu must not dismiss it before the click
    // action runs (MenuClient would already be cleared).
    if (type == "mousedown" && !overMenu) {
      Panel->closeContextMenu();
    }
  }

  ScoreboardPanel *Panel = nullptr;
};

ScoreboardPanel::ScoreboardPanel()
    : HudPanel("scoreboard", "abc/abcui/hud/rml/scoreboard.rml") {}

ScoreboardPanel::~ScoreboardPanel() {
  setMouseMode(false);
  if (Listener) {
    if (auto *box = element("box")) {
      box->RemoveEventListener("mousedown", Listener, false);
      box->RemoveEventListener("click", Listener, false);
    }
    Listener->Panel = nullptr;
  }
  delete Listener;
}

void ScoreboardPanel::reset() {
  Intermission = false;
  InScore = false;
  setVisible(false);
  ServerName.clear();
  NextMap.clear();
  TimeEndCount = 0;
  MenuClient = 0;
  DataVersion++;
  if (Document) {
    closeContextMenu();
    setVisible(false);
    setMouseMode(false);
  }
}

void ScoreboardPanel::setVisible(bool visible) {
  if (!visible)
    setMouseMode(false);
  if (visible == Visible)
    return;
  Visible = visible;
  // The document itself is the visibility switch (HudPanel::show is
  // idempotent and focus-safe); the box rides it.
  visible ? show() : hide();
  if (!visible)
    closeContextMenu();
  // Old ShowPanel: a fresh open refreshes everything immediately.
  if (visible) {
    DataVersion++;
    LastRefresh = -1.0;
  }
}

void ScoreboardPanel::setMouseMode(bool enabled) {
  enabled = enabled && Visible && isVisible();
  // The router is the single ownership truth (T06); MouseMode mirrors it.
  // After a force-release (focus loss) a re-enable must re-acquire even
  // though this flag still reads enabled.
  if (enabled == MouseMode && (!enabled || PointerToken.held()))
    return;
  MouseMode = enabled;
  // Ownership gates both Rml input and the client mouse/view hooks; the
  // native cursor lifetime follows the router's first/last owner edge.
  if (enabled)
    PointerToken = rmlui::uiRouter().acquire(
        this, framework::uiinput::OwnerTier::kInteractiveHud);
  else
    PointerToken.release();
  if (enabled) {
    vgui2ext::centerAndUnlockCursor();
  } else {
    closeContextMenu();
  }
}

// uiinput::Owner: consulted only while this panel's token is top-of-stack.
bool ScoreboardPanel::claimsKey(int down, int keynum) {
  (void)down;
  return MouseMode && ((keynum >= K_MOUSE1 && keynum <= K_MOUSE5) ||
                       keynum == K_MWHEELUP || keynum == K_MWHEELDOWN);
}

bool ScoreboardPanel::claimsWheel() {
  return MouseMode;
}

void ScoreboardPanel::onOwnershipLost() {
  if (MouseMode)
    setMouseMode(false);
}

bool ScoreboardPanel::answerKey(int down, int keynum) {
  if (!Visible || !isVisible())
    return false;
  if (keynum == kMouse2 && down && !MouseMode) {
    // Enter once; subsequent right presses belong to the row context menu.
    setMouseMode(true);
    return true;
  }
  return MouseMode && ((keynum >= K_MOUSE1 && keynum <= K_MOUSE5) ||
                       keynum == K_MWHEELUP || keynum == K_MWHEELDOWN);
}

void ScoreboardPanel::onDocumentReady() {
  ServerName = hudstate::messages().ServerName;
  NextMap = hudstate::messages().NextMap;
  TimeEndCount = hudstate::messages().TimeEnd;
  TimeEndBase = ClientTimeNow;
  if (abc::CChainedCommand *open = commands::get("+showscores")) {
    subscribe(open->Event(), [this]() {
      if (!Intermission) {
        setVisible(true);
        // Old Viewport m_bInScore: the button state the usercmd stream
        // reports (IN_SCORE) - the server streams ping/loss only to
        // clients it believes hold the scoreboard (issue #9).
        InScore = true;
      }
      return false;
    });
  }
  if (abc::CChainedCommand *close = commands::get("-showscores")) {
    subscribe(close->Event(), [this]() {
      if (!Intermission) {
        setVisible(false);
        setMouseMode(false);
      }
      InScore = false;
      return false;
    });
  }
  subscribe(netmsg::ServerName, [this](const char *name) {
    ServerName = name ? name : "";
    DataVersion++;
  });
  subscribe(netmsg::NextMap, [this](const char *map) {
    NextMap = map ? map : "";
    DataVersion++;
  });
  subscribe(netmsg::TimeEnd, [this](int seconds) {
    TimeEndCount = seconds;
    TimeEndBase = ClientTimeNow;
    DataVersion++;
  });
  subscribe(netmsg::ScoreInfo, [this](int, float, int, float, float, int, int,
                                      int) { DataVersion++; });

  if (!Listener) {
    Listener = new TableListener();
    Listener->Panel = this;
  }
  if (Rml::Element *box = element("box")) {
    box->AddEventListener("mousedown", Listener, false);
    box->AddEventListener("click", Listener, false);
  }
  setVisible(false);
}

bool ScoreboardPanel::setHeaderRml(int slot, const char *id,
                                   const std::string &rml) {
  if (slot < 0 || slot >= static_cast<int>(std::size(HeaderCache)) ||
      HeaderCache[slot] == rml)
    return false;
  if (Rml::Element *node = element(id)) {
    node->SetInnerRML(rml);
    HeaderCache[slot] = rml;
    return true;
  }
  return false;
}

void ScoreboardPanel::updateHeader() {
  // Every write is change-gated (setHeaderRml): this runs per frame while
  // the board is up.
  setHeaderRml(0, "server-name", framework::TextUtil::escapeRml(ServerName));
  {
    const char *level = gEngfuncs.pfnGetLevelName();
    const char *slash =
        level ? (std::max)(strrchr(level, '/'), strrchr(level, '\\')) : nullptr;
    std::string base = slash ? slash + 1 : (level ? level : "");
    // Old V_FileBase stripped the extension too ("mapname" not "map.bsp").
    if (const size_t dot = base.rfind('.'); dot != std::string::npos)
      base.resize(dot);
    setHeaderRml(1, "map-name", framework::TextUtil::escapeRml(base));
  }
  {
    int connected = 0;
    for (int i = 1; i <= playerres::kMaxPlayers; i++) {
      if (const playerres::PlayerInfo *info = playerres::getPlayerInfo(i);
          info && info->isValid())
        connected++;
    }
    setHeaderRml(2, "player-count",
                 strFmt("%d/%d", connected, gEngfuncs.GetMaxClients()));
  }
  {
    // NextMap is server text - composed, never a fixed buffer.
    const std::string label = localized("Scores_NextMap", "Next Map: ");
    setHeaderRml(3, "next-map",
                 framework::TextUtil::escapeRml(label + NextMap));
  }
  {
    // Old OnThink: the time label kept its last value until a TimeEnd
    // arrived (m_iTimeEndCount<=0 -> no refresh, never "00:00").
    if (TimeEndCount > 0) {
      const int remaining = std::max(
          0, TimeEndCount - static_cast<int>(ClientTimeNow - TimeEndBase));
      setHeaderRml(4, "time-end",
                   strFmt("%.1d:%.2d", remaining / 60, remaining % 60));
    }
  }
}

void ScoreboardPanel::rebuildTable() {
  Rml::Element *table = element("table");
  if (!table)
    return;

  // Group connected players by team, sort teams by score then players by
  // frags (old RefreshItems + StaticPlayerSortFuncByFrags, D-17 notes).
  struct RowRef {
    playerres::PlayerInfo *info;
  };
  std::map<teamres::TeamId, std::vector<RowRef>, std::greater<>> sections;
  for (int i = 1; i <= playerres::kMaxPlayers; i++) {
    playerres::PlayerInfo *info = playerres::getPlayerInfo(i);
    if (!info || !info->isValid())
      continue;
    info->update();
    sections[info->team].push_back({info});
  }

  struct TeamScore {
    int frags = 0;
    int deaths = 0;
  };
  std::map<teamres::TeamId, TeamScore> teamScores;
  for (auto &[team, rows] : sections) {
    for (RowRef &row : rows) {
      teamScores[team].frags += static_cast<int>(row.info->frags);
      teamScores[team].deaths += row.info->deaths;
    }
    std::sort(rows.begin(), rows.end(), [](const RowRef &a, const RowRef &b) {
      if (a.info->frags != b.info->frags)
        return a.info->frags > b.info->frags;
      if (a.info->deaths != b.info->deaths)
        return a.info->deaths < b.info->deaths;
      return a.info->index < b.info->index;
    });
  }
  // Team order: frags desc, deaths asc, id desc (old comparator).
  std::vector<teamres::TeamId> order;
  for (const auto &[team, rows] : sections)
    order.push_back(team);
  std::sort(order.begin(), order.end(),
            [&](teamres::TeamId a, teamres::TeamId b) {
              const TeamScore &sa = teamScores[a];
              const TeamScore &sb = teamScores[b];
              if (sa.frags != sb.frags)
                return sa.frags > sb.frags;
              if (sa.deaths != sb.deaths)
                return sa.deaths < sb.deaths;
              return a > b;
            });

  std::string rml;
  rml.reserve(8192);

  const bool showSteamId =
      scoreboard::ShowSteamId && scoreboard::ShowSteamId->value > 0;
  const bool showLoss = scoreboard::ShowLoss && scoreboard::ShowLoss->value > 0;
  const bool showRealName =
      scoreboard::ShowRealName && scoreboard::ShowRealName->value > 0;
  // The name column takes the SteamID width when that column is hidden,
  // keeping the table's 552dp rhythm (old GetNameColumnWidth).
  table->SetClass("no-steamid", !showSteamId);

  auto headerRow = [&](bool withLabels) {
    rml += "<div class='row header'>";
    rml += "<div class='cell c-avatar'></div><div class='cell c-donor'></div>";
    if (withLabels) {
      rml += "<div class='cell c-name'>";
      rml += framework::TextUtil::escapeRml(
          localized("Scores_PlayerName", "Name"));
      rml += "</div>";
      if (showSteamId) {
        rml += "<div class='cell c-steamid'>";
        rml += framework::TextUtil::escapeRml(
            localized("Scores_SteamID", "SteamID"));
        rml += "</div>";
      }
      rml += "<div class='cell c-health'>";
      rml += framework::TextUtil::escapeRml(
          localized("Scores_PlayerHealth", "HP"));
      rml += "</div><div class='cell c-armor'>";
      rml +=
          framework::TextUtil::escapeRml(localized("Scores_PlayerArmor", "AP"));
      rml += "</div><div class='cell c-frags'>";
      rml += framework::TextUtil::escapeRml(
          localized("Scores_PlayerScore", "Frags"));
      rml += "</div><div class='cell c-deaths'>";
      rml += framework::TextUtil::escapeRml(
          localized("Scores_PlayerDeath", "Death"));
      rml += "</div><div class='cell c-ping'>";
      rml += framework::TextUtil::escapeRml(
          localized(showLoss ? "Scores_lPingLoss" : "Scores_Ping",
                    showLoss ? "Ping/Loss" : "Ping"));
      rml += "</div>";
    } else {
      rml += "<div class='cell c-name'></div>";
      if (showSteamId)
        rml += "<div class='cell c-steamid'></div>";
      rml +=
          "<div class='cell c-health'></div><div class='cell c-armor'></div>";
      rml += "<div class='cell c-frags'></div><div class='cell "
             "c-deaths'></div><div class='cell c-ping'></div>";
    }
    rml += "</div>";
  };
  headerRow(true);

  const std::string na = localized("Scores_NA", "N/A");
  const std::string specTag = localized("Scores_PlayerSpec", "(Spectator)");
  const std::string deadTag = localized("Scores_PlayerHealthDead", "DEAD");

  // Old section header "(n/x)": x was the whole server's connected count.
  int connectedTotal = 0;
  for (int i = 1; i <= playerres::kMaxPlayers; i++) {
    if (const playerres::PlayerInfo *info = playerres::getPlayerInfo(i);
        info && info->isValid())
      connectedTotal++;
  }

  // Old SectionedListPanel: the column-label header doubled as the only
  // section when the server ran team-less (no "Team_None (n/x)" strip).
  const bool singleTeamNone =
      order.size() == 1 && order[0] == teamres::TeamId::None;

  auto appendDonorIcon = [&](const playerres::PlayerInfo &info) {
    const char *icon = "";
    switch (info.donor) {
    case playerres::Donor::ElectricCrowbar:
      icon = "donor_1";
      break;
    case playerres::Donor::GoldedUzi:
      icon = "donor_2";
      break;
    case playerres::Donor::GoldedDollar:
      icon = "donor_3";
      break;
    case playerres::Donor::Tester:
      icon = "donor_4";
      break;
    case playerres::Donor::Artist:
      icon = "donor_5";
      break;
    case playerres::Donor::Developer:
      icon = "donor_6";
      break;
    default:
      break;
    }
    if (!icon[0]) {
      rml += "<div class='cell c-donor'></div>";
      return;
    }
    appendFmt(rml, "<div class='cell c-donor'><img src='../img/%s.png'/></div>",
              icon);
  };

  // The admin badge paints over the avatar square (old CAvatarImage::Paint).
  auto appendAvatar = [&](const playerres::PlayerInfo &info) {
    if (!(scoreboard::ShowAvatars && scoreboard::ShowAvatars->value > 0)) {
      rml += "<div class='cell c-avatar'></div>";
      return;
    }
    // The ?gen suffix cache-busts Rml's per-source texture database once
    // the RGBA landed (avatars bumps the generation on upload).
    appendFmt(rml,
              "<div class='cell c-avatar'><div class='avatar-frame'>"
              "<img class='avatar' src='avatar://%llu?gen=%d'/>",
              static_cast<unsigned long long>(info.steamId64),
              avatars::generation(info.steamId64));
    if (info.admin == playerres::Admin::Operator)
      rml += "<img class='admin' src='../img/admin_icon32.png'/>";
    else if (info.admin == playerres::Admin::ServerOwner)
      rml += "<img class='admin' src='../img/serverowner_icon32.png'/>";
    // Issue 17: the muted badge (old SetMuted overlay) reads the voice
    // service's ban state - the same truth the context menu toggles.
    if (services::voice::status().isPlayerBlocked(info.index))
      rml += "<img class='mute' src='../img/mute_icon32.png'/>";
    rml += "</div></div>";
  };

  for (teamres::TeamId team : order) {
    const std::vector<RowRef> &rows = sections[team];
    // Section header: team name (player share of the server) + team score.
    if (!singleTeamNone) {
      const char *token = teamres::teamNameToken(team);
      std::string teamName = localized(token, token);
      appendFmt(
          rml,
          "<div class='row section'>"
          "<div class='cell c-avatar'></div><div class='cell c-donor'></div>"
          "<div class='cell c-name'>%s (%d/%d)</div>",
          framework::TextUtil::escapeRml(teamName).c_str(),
          static_cast<int>(rows.size()), connectedTotal);
      if (showSteamId)
        rml += "<div class='cell c-steamid'></div>";
      rml +=
          "<div class='cell c-health'></div><div class='cell c-armor'></div>";
      appendFmt(rml, "<div class='cell c-frags'>%d</div>",
                teamScores[team].frags);
      appendFmt(rml, "<div class='cell c-deaths'>%d</div>",
                teamScores[team].deaths);
      rml += "<div class='cell c-ping'></div></div>";
    }

    for (const RowRef &row : rows) {
      const playerres::PlayerInfo &info = *row.info;
      // Issue #9: ping/loss are a live engine mirror and are NOT carried by
      // the ScoreInfo wire - re-read them for this row right before it is
      // formatted (the legacy per-row UpdatePing). The 0.5 s rebuild is the
      // only periodic read; without it the column kept whatever value the
      // last ScoreInfo/Spectator/ClExtras event left behind.
      row.info->refreshPingLoss();
      // Row fg/bg ride the client's per-player tint (old SetItemFgColor
      // GetPlayerColor; bg = same tint at alpha 80 unless this player).
      std::string rowOpen = "<div class='row";
      if (info.isThisPlayer())
        rowOpen += " thisplayer";
      int pr = 0, pg = 0, pb = 0;
      if (playerColor(info.index, pr, pg, pb)) {
        // Row fg/bg ride the client's per-player tint (old
        // SetItemFgColor GetPlayerColor; bg = same tint at alpha 80
        // unless this player).
        appendFmt(rowOpen, "' style='color: rgba(%d, %d, %d, 255);", pr, pg,
                  pb);
        if (!info.isThisPlayer())
          appendFmt(rowOpen, " background-color: rgba(%d, %d, %d, 80);", pr, pg,
                    pb);
      }
      if (rowOpen.find("style=") == std::string::npos)
        rowOpen += "' style='";
      appendFmt(rowOpen, " height: %.1fpx;' data-client='%d'>",
                AppliedRowHeight > 0 ? AppliedRowHeight : 28.0f, info.index);
      rml += rowOpen;

      appendAvatar(info);
      appendDonorIcon(info);

      // Name (+ real name when the backend can serve it).
      {
        std::string name = info.name;
        if (showRealName && !info.realName.empty()) {
          name += " (";
          name += info.realName;
          name += ")";
        }
        appendFmt(rml, "<div class='cell c-name'>%s</div>",
                  framework::TextUtil::escapeRml(name).c_str());
      }

      if (showSteamId) {
        // hud_scoreboard_showsteamidtype: 64-bit form vs STEAM_0:X:Y
        // (old m_szSteamID64 vs m_pSteamId.Render()).
        std::string steam;
        if (info.steamId64 == 0) {
          // old m_szSteamID64 stayed empty when invalid
        } else if (scoreboard::ShowSteamIdType &&
                   scoreboard::ShowSteamIdType->value > 0) {
          steam = std::format("{}", info.steamId64);
        } else {
          const std::uint32_t accountId =
              static_cast<std::uint32_t>(info.steamId64 & 0xFFFFFFFFull);
          steam = std::format("STEAM_0:{}:{}", accountId & 1, accountId >> 1);
        }
        appendFmt(rml, "<div class='cell c-steamid'>%s</div>",
                  framework::TextUtil::escapeRml(steam).c_str());
      }

      // Health / armor (spectators show the grey tag; negative = dead;
      // both carry the old COLOR_PERCENT hue ramp).
      if (info.spectate) {
        appendFmt(rml,
                  "<div class='cell c-health' style='color: rgba(128, 128, "
                  "128, 255);'>%s</div>"
                  "<div class='cell c-armor' style='color: rgba(128, 128, 128, "
                  "255);'>%s</div>",
                  framework::TextUtil::escapeRml(specTag).c_str(),
                  framework::TextUtil::escapeRml(na).c_str());
      } else {
        if (info.health < 0) {
          appendFmt(rml, "<div class='cell c-health'>%s</div>",
                    framework::TextUtil::escapeRml(deadTag).c_str());
        } else {
          appendFmt(
              rml, "<div class='cell c-health' style='color: %s;'>%d</div>",
              percentColor(info.health).c_str(), static_cast<int>(info.health));
        }
        if (info.armor < 0) {
          appendFmt(rml, "<div class='cell c-armor'>%s</div>",
                    framework::TextUtil::escapeRml(na).c_str());
        } else {
          appendFmt(
              rml, "<div class='cell c-armor' style='color: %s;'>%d</div>",
              percentColor(info.armor).c_str(), static_cast<int>(info.armor));
        }
      }

      appendFmt(rml,
                "<div class='cell c-frags'>%d</div><div class='cell "
                "c-deaths'>%d</div>",
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

  table->SetInnerRML(rml);
  const float rowHeight = AppliedRowHeight > 0 ? AppliedRowHeight : 28.0f;
  const float avatarSize = std::max(
      1.0f, std::min(28.0f * framework::Theme::scale(), rowHeight - 2.0f));
  const float donorSize = std::max(
      1.0f, std::min(18.0f * framework::Theme::scale(), rowHeight - 2.0f));
  Rml::ElementList frames;
  table->GetElementsByClassName(frames, "avatar-frame");
  for (auto *frame : frames) {
    frame->SetProperty("width", std::to_string(avatarSize) + "px");
    frame->SetProperty("height", std::to_string(avatarSize) + "px");
  }
  Rml::ElementList images;
  table->GetElementsByTagName(images, "img");
  for (auto *image : images) {
    const bool donor =
        image->GetParentNode() && image->GetParentNode()->IsClassSet("c-donor");
    const bool avatar = image->GetParentNode() &&
                        image->GetParentNode()->IsClassSet("avatar-frame");
    if (avatar) {
      // The frame owns the size; both the Steam avatar and the admin
      // badge fill it exactly.
      image->SetProperty("width", "100%");
      image->SetProperty("height", "100%");
      continue;
    }
    const float size = donor ? donorSize : avatarSize;
    image->SetProperty("width", std::to_string(size) + "px");
    image->SetProperty("height", std::to_string(size) + "px");
  }
  table->SetProperty("line-height", std::to_string(rowHeight) + "px");
}

void ScoreboardPanel::openContextMenu(int clientIndex,
                                      const Rml::Vector2f &position) {
  const playerres::PlayerInfo *info = playerres::getPlayerInfo(clientIndex);
  if (!info || !info->isValid())
    return;
  MenuClient = clientIndex;
  Rml::Element *menu = element("menu");
  if (!menu)
    return;

  const bool hasSteamId = info->steamId64 != 0;
  const bool mutablePlayer = hasSteamId && !info->isThisPlayer() &&
                             services::voice::status().available();
  struct Entry {
    const char *id;
    ScoreboardAction action;
    const char *token;
    const char *fallback;
  };
  const Entry entries[] = {
      {"menu-profile", ScoreboardAction::kOpenProfile,
       "Scores_MenuSteamProfile", "Open Steam Profile"},
      {"menu-copy-url", ScoreboardAction::kCopyProfileUrl,
       "Scores_MenuSteamURL", "Copy Profile URL"},
      {"menu-copy-name", ScoreboardAction::kCopySteamName,
       "Scores_MenuCopyName", "Copy Steam Name"},
      {"menu-copy-name-raw", ScoreboardAction::kCopyGameName,
       "Scores_MenuCopyNameRaw", "Copy Game Name"},
      {"menu-copy-steamid", ScoreboardAction::kCopySteamId,
       "Scores_MenuCopySteamID", "Copy SteamID"},
      {"menu-copy-steamid64", ScoreboardAction::kCopySteamId64,
       "Scores_MenuCopySteamID64", "Copy SteamID64"},
  };
  for (const Entry &entry : entries) {
    if (Rml::Element *item = element(entry.id)) {
      item->SetInnerRML(framework::TextUtil::escapeRml(
          localized(entry.token, entry.fallback)));
      item->SetAttribute("data-action",
                         std::to_string(static_cast<int>(entry.action)));
    }
  }
  if (Rml::Element *item = element("menu-profile"))
    item->SetProperty("display", hasSteamId ? "block" : "none");
  if (Rml::Element *item = element("menu-copy-name"))
    item->SetProperty("display", info->realName.empty() ? "none" : "block");
  if (Rml::Element *item = element("menu-mute")) {
    if (mutablePlayer) {
      // The label follows the backend's current truth (the legacy menu
      // flipped the entry with IsPlayerBlocked) - never the last click.
      const bool blocked =
          services::voice::status().isPlayerBlocked(MenuClient);
      item->SetInnerRML(framework::TextUtil::escapeRml(
          localized(blocked ? "Scores_MenuUnmute" : "Scores_MenuMute",
                    blocked ? "Unmute Voice" : "Mute Voice")));
      item->SetAttribute("data-action", std::to_string(static_cast<int>(
                                            ScoreboardAction::kMute)));
      item->SetProperty("display", "block");
    } else {
      // Bots, the local player and SteamID-less rows have nothing to mute.
      item->SetProperty("display", "none");
    }
  }

  // Position at the pointer, clamped into the window (old
  // PositionRelativeToPanel CURSOR mode). #menu sits inside #box (a
  // centered absolute container) - its left/top are relative to the box
  // origin, so clamp in context space first, then subtract the box offset.
  Rml::Vector2f at = position;
  if (Rml::Context *context = rmlui::context()) {
    at.x = std::clamp(
        at.x, 0.0f,
        std::max(0.0f, static_cast<float>(context->GetDimensions().x) -
                           180.0f * framework::Theme::scale()));
    at.y = std::clamp(
        at.y, 0.0f,
        std::max(0.0f, static_cast<float>(context->GetDimensions().y) -
                           100.0f * framework::Theme::scale()));
  }
  if (Rml::Element *box = element("box")) {
    const Rml::Vector2f origin = box->GetAbsoluteOffset();
    at.x -= origin.x;
    at.y -= origin.y;
  }
  menu->SetProperty("left", std::to_string(at.x) + "px");
  menu->SetProperty("top", std::to_string(at.y) + "px");
  menu->SetProperty("display", "block");
}

bool ScoreboardPanel::overMenu(const Rml::Vector2f &position) {
  Rml::Element *menu = element("menu");
  if (!menu || MenuClient == 0)
    return false;
  const Rml::Vector2f origin = menu->GetAbsoluteOffset();
  const Rml::Vector2f size = menu->GetBox().GetSize(Rml::BoxArea::Content);
  return position.x >= origin.x && position.x <= origin.x + size.x &&
         position.y >= origin.y && position.y <= origin.y + size.y;
}

void ScoreboardPanel::closeContextMenu() {
  MenuClient = 0;
  if (Rml::Element *menu = element("menu"))
    menu->SetProperty("display", "none");
}

void ScoreboardPanel::menuAction(ScoreboardAction action) {
  const playerres::PlayerInfo *info = playerres::getPlayerInfo(MenuClient);
  if (!info || !info->isValid())
    return;

  auto copy = [](const std::string &text) {
    if (Rml::SystemInterface *system = Rml::GetSystemInterface())
      system->SetClipboardText(text);
  };
  const std::string profileUrl =
      std::format("http://steamcommunity.com/profiles/{}", info->steamId64);
  switch (action) {
  case ScoreboardAction::kOpenProfile:
    // Old overlay path needed SteamFriends; the browser fallback is
    // unconditional here.
    framework::OpenExternalUrl(profileUrl);
    break;
  case ScoreboardAction::kCopyProfileUrl:
    copy(profileUrl);
    break;
  case ScoreboardAction::kCopySteamName:
    copy(info->realName);
    break;
  case ScoreboardAction::kCopyGameName:
    copy(info->name);
    break;
  case ScoreboardAction::kCopySteamId: {
    // Old m_pSteamId.Render(): STEAM_0:X:Y form from the 64-bit id.
    const std::uint32_t accountId =
        static_cast<std::uint32_t>(info->steamId64 & 0xFFFFFFFFull);
    copy(std::format("STEAM_0:{}:{}", accountId & 1, accountId >> 1));
    break;
  }
  case ScoreboardAction::kCopySteamId64:
    copy(std::to_string(info->steamId64));
    break;
  case ScoreboardAction::kMute:
    // Issue 17: the toggle goes through the voice service (persisted ban +
    // server sync). Read-back discipline: the rebuild only moves when the
    // service accepted - a refused toggle keeps the row and the menu label
    // exactly as they were.
    if (services::voice::status().setPlayerBlockedState(
            MenuClient, !services::voice::status().isPlayerBlocked(MenuClient)))
      ++DataVersion;
    break;
  }
}

void ScoreboardPanel::onData(double frameTime) {
  ClientTimeNow += frameTime;
  if (!Document)
    return;

  // Mouse mode rides visibility: closing the scoreboard always re-locks.
  if (!Visible || !isVisible()) {
    setMouseMode(false);
    return;
  }

  const float rawMode = scoreboard::Size ? scoreboard::Size->value : 0;
  const int mode = std::isfinite(rawMode)
                       ? static_cast<int>(std::clamp(rawMode, 0.0f, 2.0f))
                       : 0;
  int playerCount = 0;
  for (int i = 1; i <= playerres::kMaxPlayers; ++i) {
    auto *info = playerres::getPlayerInfo(i);
    if (info && info->isValid())
      ++playerCount;
  }
  const float density = framework::Theme::scale();
  const float screenHeight =
      rmlui::context() ? static_cast<float>(rmlui::context()->GetDimensions().y)
                       : 1080.0f;
  const auto spacing = [](cvar_t *cvar, float fallback) {
    return cvar && std::isfinite(cvar->value) && cvar->value > 0
               ? std::clamp(cvar->value, 8.0f, 128.0f)
               : fallback;
  };
  const float normal = spacing(scoreboard::SpacingNormal, 28.0f * density);
  const float compactDefault = screenHeight < 600    ? 18.0f
                               : screenHeight < 720  ? 22.0f
                               : screenHeight < 800  ? 24.0f
                               : screenHeight < 1024 ? 26.0f
                                                     : 28.0f;
  const bool compact =
      mode == 2 || (mode == 0 && (playerCount + 2) * normal > 420.0f * density);
  const float height =
      compact ? spacing(scoreboard::SpacingCompact, compactDefault) : normal;
  if (height != AppliedRowHeight) {
    AppliedRowHeight = height;
    DataVersion++;
  }
  updateHeader();
  // Avatar download pump (old CAvatarImage::Paint retry): the steamapi
  // fetch only lands when someone re-asks, and Rml caches textures per
  // source string - failures included - so the row's avatar:// URL never
  // re-resolves on its own. Poll every connected player while the board is
  // up; a landing upload rebuilds immediately (the fresh row emits a new
  // ?gen URL, which is what finally reaches Rml).
  if (scoreboard::ShowAvatars && scoreboard::ShowAvatars->value > 0) {
    for (int index = 1; index <= playerres::kMaxPlayers; index++) {
      const playerres::PlayerInfo *info = playerres::getPlayerInfo(index);
      if (info && info->connected && info->steamId64 &&
          avatars::touch(info->steamId64))
        DataVersion++;
    }
  }
  // Old OnThink: 0.5s full refresh cadence; data-version bumps refresh
  // immediately. The table only rebuilds while shown (old
  // UpdateOnPlayerInfo IsVisible guard).
  if (DataVersion != AppliedVersion || ClientTimeNow - LastRefresh >= 0.5) {
    AppliedVersion = DataVersion;
    LastRefresh = ClientTimeNow;
    rebuildTable();
  }

  // Vertical centering (old Resize): the box hugs its content height.
  // Change-gated (Phase 8): an unconditional SetProperty("top") would dirty
  // the document every frame and re-layout the table with it.
  if (Rml::Context *context = rmlui::context()) {
    if (Rml::Element *box = element("box")) {
      const float contentHeight =
          box->GetBox().GetSize(Rml::BoxArea::Content).y;
      if (contentHeight != CenteredHeight) {
        CenteredHeight = contentHeight;
        const float top =
            std::max(0.0f, (static_cast<float>(context->GetDimensions().y) -
                            contentHeight) *
                               0.5f);
        box->SetProperty("top", std::to_string(top) + "px");
      }
    }
  }
}

} // namespace panels
