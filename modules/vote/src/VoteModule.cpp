// abcui.vote - the yes/no vote callout. Moved from src/panels/vote (old
// CVotePanel, ABCEnchance sources: src/hud/vgui/vote.*). The VoteMenu
// usermsg feeds the panel while the hud_vote gate is on (the framework's
// netmsg consumer passes the message through untouched when it is off), the
// bound yes/no keys (hud_votekey_yes/no, engine keynums) answer through the
// input.key veto channel and send voteyes/voteno, and EndVote hides the
// callout.
//
// The old StartVote rebuilt typed votes (kill/kick/ban/map) through English
// template regex extraction + localize reassembly; a non-English server text
// silently fell back to the raw string anyway. The port shows the server text
// directly (the server owns its localization) and keeps the localized
// default captions next to the bound key names.
#include "../../common/UiText.h"
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <cstdio>
#include <string>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};

// Old CVotePanel ctor defaults (persisted contract - never rename).
constexpr const char *kHudVote = "hud_vote";
constexpr const char *kKeyYes = "hud_votekey_yes";
constexpr const char *kKeyNo = "hud_votekey_no";
constexpr int kDefaultYesKey = 92;
constexpr int kDefaultNoKey = 93;

constexpr const char *kPanelName = "vote";
constexpr const char *kDocumentPath = "mod://abcui.vote/hud/rml/vote.rml";

AbcUiCvarHandle HudVote = nullptr;
AbcUiCvarHandle KeyYes = nullptr;
AbcUiCvarHandle KeyNo = nullptr;
bool Visible = false;

AbcUiDocument *panelDocument() {
  return Api->ui->panelDocument(kPanelName);
}

void setInnerRml(const char *id, const char *rml) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (AbcUiElement *element = Api->ui->querySelector(doc, id))
    Api->ui->elementSetInnerRML(element, rml);
}

// Measure-then-copy over the bounded C API (server text is untrusted).
std::string escapeText(const char *text) {
  return moduleui::escapeText(Api, text);
}

std::string keyName(int keynum) {
  return moduleui::readString(Api->ui->keyNameForKey, keynum);
}

std::string defaultCaption(const char *token, const char *fallback) {
  char out[512];
  return Api->ui->translate(token, out, sizeof(out)) ? std::string(out)
                                                     : std::string(fallback);
}

int boundKey(AbcUiCvarHandle cvar, int fallback) {
  return cvar ? static_cast<int>(
                    Api->console->cvarValue(cvar, static_cast<float>(fallback)))
              : fallback;
}

void setVisible(bool visible) {
  if (visible == Visible)
    return;
  Visible = visible;
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (visible)
    Api->ui->documentShow(doc);
  else
    Api->ui->documentHide(doc);
}

// Old KeyCodeTyped: the bound yes/no key while the callout is on screen
// answers and hides it. Returns true when the press was consumed.
int answerKey(int down, int keynum) {
  if (!down || !Visible)
    return 0;
  AbcUiDocument *doc = panelDocument();
  if (!doc || !Api->ui->documentVisible(doc))
    return 0;
  const int yes = boundKey(KeyYes, kDefaultYesKey);
  const int no = boundKey(KeyNo, kDefaultNoKey);
  if (keynum != yes && keynum != no)
    return 0;
  Api->console->serverCommand(keynum == yes ? "voteyes" : "voteno");
  setVisible(false);
  return 1;
}

// Old StartVote: server text in the content panel, the answer rows carry the
// bound key name plus the server caption (typed/default votes fall back to
// the localized default caption).
// The caption is server text; the vote type carries no layout.
void startVote([[maybe_unused]] int type, const char *content, const char *yes,
               const char *no) {
  const std::string escapedContent = escapeText(content);
  setInnerRml("content", escapedContent.c_str());

  const std::string yesCaption =
      yes && *yes ? std::string(yes) : defaultCaption("Vote_DefaultYes", "Yes");
  const std::string noCaption =
      no && *no ? std::string(no) : defaultCaption("Vote_DefaultNo", "No");
  const std::string yesLabel =
      keyName(boundKey(KeyYes, kDefaultYesKey)) + " " + yesCaption;
  const std::string noLabel =
      keyName(boundKey(KeyNo, kDefaultNoKey)) + " " + noCaption;
  const std::string escapedYes = escapeText(yesLabel.c_str());
  const std::string escapedNo = escapeText(noLabel.c_str());
  setInnerRml("yes-text", escapedYes.c_str());
  setInnerRml("no-text", escapedNo.c_str());

  setVisible(true);
}

// ---- event sinks ----

int onVoteMenu(void *, const void *payload) {
  if (!panelDocument())
    return 0;
  const auto *event = static_cast<const AbcUiVoteMenuEvent *>(payload);
  startVote(event->type, event->content, event->yes, event->no);
  return 1;
}

void onEndVote(void *, const void *) {
  setVisible(false);
}

int onKeyEvent(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiKeyEvent *>(payload);
  return answerKey(event->down, event->keynum);
}

// ---- panel callbacks ----

void panelDocumentReady(void *) {
  // Old ctor label: the static "#abcui.vote/Vote_DefaultMessage" title strip.
  const std::string title =
      escapeText(defaultCaption("Vote_DefaultMessage", "").c_str());
  setInnerRml("title", title.c_str());
  setVisible(false);
}

void panelOnData(void *, double) {
  // Old onData: a vote dismissed through hud_vote (the wire consumer stops
  // routing when the gate is off) must not stay on screen. `<= 0` is the old
  // test; an unregistered gate keeps the current state.
  if (HudVote && Api->console->cvarValue(HudVote, 1.0f) <= 0.0f)
    setVisible(false);
}

void panelReset(void *) {
  setVisible(false);
}

// ---- lifecycle ----

void onModuleLoad() {
  Panel.structSize = sizeof(Panel);
  Panel.name = kPanelName;
  Panel.documentPath = kDocumentPath;
  Panel.zIndex = 0;
  Panel.userdata = nullptr;
  Panel.onDocumentReady = &panelDocumentReady;
  Panel.onData = &panelOnData;
  Panel.onRedraw = nullptr;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);

  Api->events->subscribeVeto(ABCUI_CHANNEL_NETMSG_VOTE_MENU, 0, &onVoteMenu,
                             nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_END_VOTE, 0, &onEndVote, nullptr);
  Api->events->subscribeVeto(ABCUI_CHANNEL_INPUT_KEY, 0, &onKeyEvent, nullptr);
}

void onModuleInit() {
  HudVote = Api->console->registerCvar(kHudVote, "1", ABCUI_CVAR_VALUE);
  KeyYes = Api->console->registerCvar(kKeyYes, "92", ABCUI_CVAR_VALUE);
  KeyNo = Api->console->registerCvar(kKeyNo, "93", ABCUI_CVAR_VALUE);
  // The VoteMenu usermsg consumer passes messages through while the gate
  // is off (old IsVoteEnable).
  Api->console->setVoteGate(HudVote);
}

void onModuleShutdown() {
  // The document dies with the context; drop the on-screen truth so a stale
  // Visible cannot swallow the answer keys in the next cycle.
  Visible = false;
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
      !api->events || !api->ui || !api->console)
    return 0;
  // The ui/console sections grew for this module; refuse a framework whose
  // tables are shorter than the members used below.
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->console->structSize < sizeof(AbcUiConsoleApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.vote";
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
