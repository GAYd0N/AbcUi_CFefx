// abcui.killfeed - the kill feed rows. Moved from
// src/panels/deadmsg (old CDeathMsgPanel, ABCEnchance sources:
// src/hud/vgui/deadmsg.*): rows fed by the textfeed.killfeed channel, capped
// by hud_deathnotice_count, rows fading out after hud_deathnotice_time
// (0.5s fade); hud_deathnotice toggles the whole feed (persisted contract).
//
// Port note kept from the built-in copy: the old AddItem console print built
// its printf with transposed Q_snprintf arguments (the victim name landed in
// the format slot); the port prints the localized "DeathMsg_ConsolePrint"
// template with a positional substitution, never a printf format.
#include "../../common/Text.h"
#include "../../common/UiText.h"
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};

// Old CDeathMsgPanel ctor defaults (persisted contract - never rename).
constexpr const char *kDeathNotice = "hud_deathnotice";
constexpr const char *kDeathNoticeTime = "hud_deathnotice_time";
constexpr const char *kDeathNoticeCount = "hud_deathnotice_count";

constexpr const char *kPanelName = "deadmsg";
constexpr const char *kDocumentPath =
    "mod://abcui.killfeed/hud/rml/deadmsg.rml";

// Old RunAnimationCommand duration.
constexpr double kFadeSeconds = 0.5;

AbcUiCvarHandle DeathNotice = nullptr;
AbcUiCvarHandle DeathNoticeTime = nullptr;
AbcUiCvarHandle DeathNoticeCount = nullptr;

struct Row {
  AbcUiElement *element = nullptr;
  double createTime = 0.0;
  double fadeStartTime = -1.0;
};

double ClientTimeNow = 0.0;
std::vector<Row> Rows;

AbcUiDocument *panelDocument() {
  return Api->ui->panelDocument(kPanelName);
}

int cvarInt(AbcUiCvarHandle cvar, int fallback) {
  return cvar ? static_cast<int>(
                    Api->console->cvarValue(cvar, static_cast<float>(fallback)))
              : fallback;
}

void setVisible(bool visible) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (visible)
    Api->ui->documentShow(doc);
  else
    Api->ui->documentHide(doc);
}

// Measure-then-copy over the bounded C API (server text is untrusted).
std::string escapeText(const char *text) {
  return moduleui::escapeText(Api, text);
}

std::string translate(const char *token, const char *fallback) {
  char out[512];
  return Api->ui->translate(token, out, sizeof(out)) ? std::string(out)
                                                     : std::string(fallback);
}

// Old console print template is a {0}..{2} template (never a printf format).
std::string substitute(const std::string &text, const char *a, const char *b,
                       const char *c) {
  const char *values[3] = {a, b, c};
  std::string out = text;
  for (int i = 0; i < 3; i++) {
    const std::string key = "{" + std::to_string(i) + "}";
    const std::string value = values[i] ? values[i] : "";
    moduleui::replaceAll(out, key, value);
  }
  return out;
}

void removeRow(Row &row) {
  if (row.element)
    Api->ui->elementRemove(row.element);
  row.element = nullptr;
}

void clearRows(AbcUiDocument *doc) {
  for (Row &row : Rows)
    removeRow(row);
  Rows.clear();
  if (doc)
    Api->ui->documentHide(doc);
}

void addItem(const char *victim, const char *attacker, const char *inflictor) {
  // Old hud_deathnotice callback (ShowDeathMsg) - evaluated live so the cvar
  // takes effect without a panel rebuild.
  if (cvarInt(DeathNotice, 1) <= 0)
    return;
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  AbcUiElement *feed = Api->ui->querySelector(doc, "feed");
  if (!feed)
    return;

  // A killfeed during a hide-all token still appends the row (it renders
  // with the document's next show), but must not un-hide the panel.
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  if (!(state.hideBits & ABCUI_HIDEBIT_ALL))
    setVisible(true);

  // Server-derived names are escaped through the shared helper (D-17 #3).
  const std::string rml = "<div class=\"attacker font-default\">" +
                          escapeText(attacker) +
                          "</div>"
                          "<div class=\"inflictor font-default\">[" +
                          escapeText(inflictor) +
                          "]</div>"
                          "<div class=\"victim font-default\">" +
                          escapeText(victim) + "</div>";
  AbcUiElement *row = Api->ui->elementAppendRml(feed, rml.c_str(), "row");
  if (!row)
    return;

  // Old capacity: hud_deathnotice_count rows, oldest dropped first.
  const int maxRows = std::max(1, cvarInt(DeathNoticeCount, 6));
  while (static_cast<int>(Rows.size()) >= maxRows) {
    removeRow(Rows.front());
    Rows.erase(Rows.begin());
  }
  Row entry;
  entry.element = row;
  entry.createTime = ClientTimeNow;
  Rows.push_back(entry);

  // Old AddItem console print (localized template). The names never touch a
  // printf format slot.
  const std::string consoleTemplate = translate("DeathMsg_ConsolePrint", "");
  if (!consoleTemplate.empty()) {
    Api->core->print(
        "%s\n",
        substitute(consoleTemplate, victim,
                   attacker && attacker[0] ? attacker : "Something", inflictor)
            .c_str());
  }
}

// ---- event sinks ----

int onKillFeed(void *, const void *payload) {
  if (!panelDocument())
    return 0;
  const auto *event = static_cast<const AbcUiKillFeedEvent *>(payload);
  addItem(event->victim ? event->victim : "",
          event->attacker ? event->attacker : "",
          event->inflictor ? event->inflictor : "");
  return 1;
}

void onHideToken(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiIntEvent *>(payload);
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  // The old viewport-wide HIDEALL token hid the feed with the rest of the
  // Viewport; rows kept fading underneath. On clear the still-alive rows
  // reappear (old Viewport SetVisible(true) revealed its child panels).
  if (event->value & ABCUI_HIDEBIT_ALL)
    setVisible(false);
  else if (!Rows.empty())
    setVisible(true);
}

// ---- panel callbacks ----

void panelOnData(void *, double frameTime) {
  ClientTimeNow += frameTime;
  if (cvarInt(DeathNotice, 1) <= 0) {
    clearRows(panelDocument());
    return;
  }
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  const double keep = static_cast<double>(cvarInt(DeathNoticeTime, 6));
  for (auto itor = Rows.begin(); itor != Rows.end();) {
    Row &row = *itor;
    if (row.fadeStartTime < 0.0) {
      // Old per-row RunAnimationCommand: fade starts keep seconds after the
      // row was added.
      if (ClientTimeNow >= row.createTime + keep) {
        row.fadeStartTime = ClientTimeNow;
        if (row.element) {
          Api->ui->elementSetProperty(row.element, "transition",
                                      "opacity 0.5s linear-in");
          Api->ui->elementSetProperty(row.element, "opacity", "0");
        }
      }
      ++itor;
      continue;
    }
    if (ClientTimeNow >= row.fadeStartTime + kFadeSeconds) {
      removeRow(row);
      itor = Rows.erase(itor);
      continue;
    }
    ++itor;
  }
  if (Rows.empty())
    Api->ui->documentHide(doc);
}

void panelReset(void *) {
  // Old Reset: clear rows + hide.
  clearRows(panelDocument());
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

  Api->events->subscribeVeto(ABCUI_CHANNEL_TEXTFEED_KILL_FEED, 0, &onKillFeed,
                             nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_HIDE_TOKEN, 0, &onHideToken,
                         nullptr);
}

void onModuleInit() {
  DeathNotice = Api->console->registerCvar(kDeathNotice, "1", ABCUI_CVAR_VALUE);
  DeathNoticeTime =
      Api->console->registerCvar(kDeathNoticeTime, "6", ABCUI_CVAR_VALUE);
  DeathNoticeCount =
      Api->console->registerCvar(kDeathNoticeCount, "6", ABCUI_CVAR_VALUE);
}

void onModuleShutdown() {
  // The document (and every appended row) dies with the context; the handles
  // must not survive into the next cycle.
  Rows.clear();
}

void onModuleUnload() {
  Api = nullptr;
}

} // namespace

ABCUI_MODULE_EXPORT int ABCUI_CALL
AbcUiModule_Main(uint32_t abiVersion, const AbcUiModuleApi *api,
                 AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api ||
      !AbcUiModule_InitDescriptor(out))
    return 0;
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->panels ||
      !api->events || !api->ui || !api->console || !api->data)
    return 0;
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->console->structSize < sizeof(AbcUiConsoleApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.killfeed";
  out->version = "2.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
