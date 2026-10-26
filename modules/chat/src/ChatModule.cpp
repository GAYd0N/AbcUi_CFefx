// abcui.chat - the chat log and the message-mode input line. Moved from
// src/panels/chat (old ChatPanel, CaptionMod sources src/chatdialog.cpp).
//
// SayText is parsed here and the native CHudSayText draw is swallowed
// through the textfeed.saytext veto, so exactly one log is on screen; the
// input line owns the keyboard through the framework input seat (the same
// token keeps the player standing still while typing), the IME candidate
// window stays framework-owned and this module only shows the language tag.
//
// Log layout: rows grow upward from the input line, the visible window is
// capped (hud_saytext_lines, at most six rows) over a 256-line scrollback,
// the wheel browses older lines while the input line is open, and the block
// fades out one second after hud_saytext_time elapses.
#include "saytext/SayText.h"

#include <abcui/events.h>
#include <abcui/module_api.h>
#include "../../common/ImeOverlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <cstring>
#include <string>
#include <vector>

namespace {

const AbcUiModuleApi *Api = nullptr;
moduleui::ImeOverlay Ime;
AbcUiPanelDesc Panel = {};

constexpr const char *kPanelName = "chat";
constexpr const char *kDocumentPath = "mod://abcui.chat/hud/rml/chat.rml";

// Old constants (chatdialog.h CHATLINE_FADE_TIME and the HUD defaults).
constexpr double kFadeSeconds = 1.0;
constexpr float kDefaultSayTextTime = 10.0f;
constexpr int kDefaultSayTextLines = 5;
constexpr size_t kMaxBuffer = 256;
constexpr int kMaxWindowRows = 6;
constexpr float kRowHeightDp = 13.0f;

// Persisted contract (never rename): abc_ui_chat is AbcUi's chat switch
// (0 = off, any other value = on, the default; while on, CaptionMod's chat
// takeover cap_newchat is forced off and restored when the switch goes back
// to 0); the hud_saytext_* set belongs to the game client and is only read.
constexpr const char *kChatMode = "abc_ui_chat";

AbcUiCvarHandle ChatMode = nullptr;
AbcUiCvarHandle SayTextEnabled = nullptr;
AbcUiCvarHandle SayTextTime = nullptr;
AbcUiCvarHandle SayTextLines = nullptr;
AbcUiCvarHandle SayTextBlockMuted = nullptr;
AbcUiCvarHandle CaptionModChat = nullptr;

enum class Mode { kNone = 0, kSay, kTeam };

struct Line {
  AbcUiElement *node = nullptr;
};

Mode InputMode = Mode::kNone;
double Clock = 0.0;
double HideAt = 0.0;
int FadeOpacity = -1;
int WindowRows = 0;
int ScrollFrames = 0;
std::vector<Line> Lines;
std::vector<std::string> History;
int HistoryCursor = -1;
std::string HistoryDraft;
AbcUiInputToken Token = 0;
std::string ImeBadge;
bool ForceNoticeHinted = false;
// cap_newchat's value before this module forced it off; the float is restored
// when the AbcUi chat is turned off.
bool CaptionModForcedOff = false;
float CaptionModSavedValue = 1.0f;

AbcUiDocument *panelDocument() {
  return Api->ui->panelDocument(kPanelName);
}

AbcUiElement *query(const char *id) {
  AbcUiDocument *doc = panelDocument();
  return doc ? Api->ui->querySelector(doc, id) : nullptr;
}

int cvarInt(AbcUiCvarHandle cvar, int fallback) {
  return cvar ? static_cast<int>(
                    Api->console->cvarValue(cvar, static_cast<float>(fallback)))
              : fallback;
}

float cvarFloat(AbcUiCvarHandle cvar, float fallback) {
  return cvar ? Api->console->cvarValue(cvar, fallback) : fallback;
}

void setProperty(AbcUiElement *element, const char *name, const char *value) {
  if (element)
    Api->ui->elementSetProperty(element, name, value);
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

// ---- visibility (abc_ui_chat / hud_saytext / CaptionMod) ----

AbcUiCvarHandle captionModChat() {
  // CaptionMod may register cap_newchat after this module's onInit; retry
  // until the engine knows it.
  if (!CaptionModChat)
    CaptionModChat = Api->console->findCvar("cap_newchat");
  return CaptionModChat;
}

bool chatEnabled() {
  return cvarInt(ChatMode, 1) > 0;
}

bool chatVisible() {
  return chatEnabled() && cvarInt(SayTextEnabled, 1) > 0;
}

// While AbcUi's chat is enabled (abc_ui_chat nonzero, the default),
// CaptionMod's takeover must be off: its hooks sit at the head of the usermsg
// and command chains (it loads after AbcUi) and swallow
// SayText/TextMsg/messagemode after handling them, so the wires never reach
// AbcUi's frames. With cap_newchat at 0 CaptionMod declines and forwards
// everything instead; the user value is remembered once and restored when the
// AbcUi chat is turned off.
void forceCaptionModOff(const AbcUiCvarHandle caption) {
  if (!caption || cvarInt(caption, 0) == 0)
    return;
  if (!CaptionModForcedOff) {
    CaptionModSavedValue = cvarFloat(caption, 1.0f);
    CaptionModForcedOff = true;
    if (!ForceNoticeHinted) {
      ForceNoticeHinted = true;
      Api->core->print("chat: CaptionMod's chat is disabled while the AbcUi "
                       "chat is on (abc_ui_chat); cap_newchat is restored "
                       "when it is turned off\n");
    }
  }
  Api->console->setCvarValue(caption, 0.0f);
}

void releaseForcedCaptionMod(const AbcUiCvarHandle caption) {
  if (!CaptionModForcedOff)
    return;
  CaptionModForcedOff = false;
  if (caption)
    Api->console->setCvarValue(caption, CaptionModSavedValue);
}

void maintainChatOwnership() {
  const AbcUiCvarHandle caption = captionModChat();
  if (!caption)
    return; // CaptionMod is not installed: nothing to arbitrate
  if (chatEnabled())
    forceCaptionModOff(caption);
  else
    releaseForcedCaptionMod(caption);
}

double sayTextTime() {
  const float value = cvarFloat(SayTextTime, 0.0f);
  return value > 0.0f ? static_cast<double>(value) : kDefaultSayTextTime;
}

int sayTextLineCount() {
  return std::clamp(cvarInt(SayTextLines, kDefaultSayTextLines), 1, 64);
}

std::string playerColorCss(int client) {
  float tint[3];
  if (!Api->players->clientColor(client, tint))
    return "rgba(255, 255, 255, 255)";
  char color[64];
  Api->ui->formatRgba(
      static_cast<int>(tint[0] * 255.0f), static_cast<int>(tint[1] * 255.0f),
      static_cast<int>(tint[2] * 255.0f), 255, color, sizeof(color));
  return color;
}

// Prefix/location ink follows the old chat scheme (amber accent).
constexpr const char *kAccentInk = "rgb(255, 176, 0)";

std::string runsToRml(const std::vector<chatsaytext::Run> &runs,
                      const std::string &nameColor) {
  std::string rml;
  rml.reserve(256);
  for (const chatsaytext::Run &run : runs) {
    switch (run.kind) {
    case chatsaytext::RunKind::kName:
      rml += "<span style=\"color: " + nameColor + ";\">" +
             escapeText(run.text) + "</span>";
      break;
    case chatsaytext::RunKind::kPrefix:
    case chatsaytext::RunKind::kLocation:
      rml += std::string("<span style=\"color: ") + kAccentInk + ";\">" +
             escapeText(run.text) + "</span>";
      break;
    case chatsaytext::RunKind::kText:
    default:
      rml += escapeText(run.text);
      break;
    }
  }
  return rml;
}

// ---- log ----

AbcUiElement *chatLog() {
  return query("chat-log");
}

bool atLogBottom() {
  AbcUiElement *log = chatLog();
  if (!log)
    return true;
  const float scrollable =
      Api->ui->elementScrollHeight(log) - Api->ui->elementClientHeight(log);
  return scrollable <= 0.0f ||
         Api->ui->elementScrollTop(log) >= scrollable - 4.0f;
}

void scrollLogToBottom() {
  if (AbcUiElement *log = chatLog())
    Api->ui->elementSetScrollTop(log, Api->ui->elementScrollHeight(log));
}

void scrollLog(float deltaDp) {
  if (AbcUiElement *log = chatLog())
    Api->ui->elementSetScrollTop(log, Api->ui->elementScrollTop(log) + deltaDp);
}

void clearLines() {
  for (Line &line : Lines) {
    if (line.node)
      Api->ui->elementRemove(line.node);
  }
  Lines.clear();
  HideAt = 0.0;
  FadeOpacity = -1;
  ScrollFrames = 0;
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

void updateFade() {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  const double remaining = HideAt - Clock;
  int percent = 100;
  if (InputMode == Mode::kNone && remaining < kFadeSeconds) {
    percent = remaining <= 0.0
                  ? 0
                  : static_cast<int>(
                        std::clamp(remaining / kFadeSeconds, 0.0, 1.0) * 100.0);
  }
  if (percent == FadeOpacity)
    return;
  FadeOpacity = percent;
  const std::string value = std::format("{:.2f}", percent / 100.0);
  Api->ui->documentSetProperty(doc, "opacity", value.c_str());
}

void updateVisibility() {
  if (!panelDocument())
    return;
  const bool wanted =
      chatVisible() && (InputMode != Mode::kNone || Clock < HideAt);
  setVisible(wanted);
}

void applyWindowHeight() {
  const int rows = std::clamp(sayTextLineCount(), 2, kMaxWindowRows);
  if (rows == WindowRows)
    return;
  WindowRows = rows;
  if (AbcUiElement *log = chatLog()) {
    const std::string value = std::format("{:.0f}dp", rows * kRowHeightDp);
    setProperty(log, "max-height", value.c_str());
  }
}

void appendNode(const std::vector<chatsaytext::Run> &runs,
                const std::string &nameColor) {
  AbcUiElement *log = chatLog();
  if (!log)
    return;
  // Follow the tail only when the reader is already there.
  const bool follow = atLogBottom();
  const std::string rml = runsToRml(runs, nameColor);
  AbcUiElement *row = Api->ui->elementAppendRml(log, rml.c_str(), "chat-line");
  if (!row)
    return;
  Lines.push_back(Line{row});
  while (Lines.size() > kMaxBuffer) {
    if (Lines.front().node)
      Api->ui->elementRemove(Lines.front().node);
    Lines.erase(Lines.begin());
  }
  HideAt = Clock + sayTextTime();
  FadeOpacity = -1;
  if (follow)
    ScrollFrames = 2;
  updateFade();
  updateVisibility();
}

// ---- input line ----

AbcUiElement *inputElement() {
  return query("chat-input");
}

std::string inputText() {
  AbcUiElement *input = inputElement();
  if (!input)
    return std::string();
  const int needed = Api->ui->elementValue(input, nullptr, 0);
  if (needed <= 0)
    return std::string();
  std::string out(static_cast<size_t>(needed) + 1, '\0');
  Api->ui->elementValue(input, out.data(), static_cast<uint32_t>(out.size()));
  out.resize(static_cast<size_t>(needed));
  return out;
}

void setInputText(const std::string &text) {
  AbcUiElement *input = inputElement();
  if (!input)
    return;
  Api->ui->elementSetValue(input, text.c_str());
  const int end = static_cast<int>(text.size());
  Api->ui->elementSetSelection(input, end, end);
}

void setPrompt(Mode mode) {
  if (AbcUiElement *prompt = query("chat-prompt")) {
    const char *token = mode == Mode::kTeam ? "Chat_Team" : "Chat_Say";
    char translated[128];
    const std::string text =
        Api->ui->translate(token, translated, sizeof(translated))
            ? std::string(translated)
            : std::format("#{}", token);
    Api->ui->elementSetInnerRML(prompt, escapeText(text).c_str());
  }
}

void updateImeBadge() {
  char tag[16] = {};
  Api->input->imeLanguageTag(tag, sizeof(tag));
  if (ImeBadge == tag)
    return;
  ImeBadge = tag;
  if (AbcUiElement *badge = query("chat-ime"))
    Api->ui->elementSetInnerRML(badge, escapeText(ImeBadge).c_str());
}

// Router callbacks (the token keeps the game from acting on the keyboard).
int32_t claimsKey(void *, int32_t, int32_t) {
  return InputMode != Mode::kNone ? 1 : 0;
}

int32_t claimsWheel(void *) {
  return InputMode != Mode::kNone ? 1 : 0;
}

int32_t blocksGameplay(void *) {
  return InputMode != Mode::kNone ? 1 : 0;
}

int32_t onWheelPulse(void *, int32_t keynum) {
  if (InputMode == Mode::kNone)
    return 0;
  if (keynum == ABCUI_KEY_MWHEELUP)
    scrollLog(-kRowHeightDp * 3.0f);
  else if (keynum == ABCUI_KEY_MWHEELDOWN)
    scrollLog(kRowHeightDp * 3.0f);
  return 1;
}

void closeInput();

void recallHistory(int step) {
  if (History.empty())
    return;
  if (HistoryCursor < 0) {
    if (step > 0)
      return;
    HistoryDraft = inputText();
    HistoryCursor = static_cast<int>(History.size()) - 1;
  } else {
    HistoryCursor += step;
    if (HistoryCursor < 0)
      HistoryCursor = 0;
    if (HistoryCursor >= static_cast<int>(History.size())) {
      HistoryCursor = -1;
      setInputText(HistoryDraft);
      return;
    }
  }
  setInputText(History[static_cast<size_t>(HistoryCursor)]);
}

void submit() {
  const std::string text = inputText();
  if (text.empty()) {
    closeInput();
    return;
  }
  // The wire the client's own chat entry uses; quotes and control bytes are
  // stripped before assembly.
  std::string payload;
  payload.reserve(text.size());
  for (char c : text) {
    if (c == '"' || static_cast<unsigned char>(c) < 0x20)
      continue;
    payload += c;
  }
  const std::string command =
      (InputMode == Mode::kTeam ? "say_team \"" : "say \"") + payload + "\"";
  Api->console->engineCommand(command.c_str());
  History.push_back(text);
  if (History.size() > 32)
    History.erase(History.begin());
  closeInput();
}

void onClaimedKeyDown(void *, int32_t keynum) {
  if (InputMode == Mode::kNone)
    return;
  // The IME owns these while composing (Enter commits, ESC cancels).
  if (Api->input->textComposing())
    return;
  switch (keynum) {
  case ABCUI_KEY_ESCAPE:
    closeInput();
    break;
  case ABCUI_KEY_ENTER:
  case ABCUI_KEY_KP_ENTER:
    submit();
    break;
  case ABCUI_KEY_UPARROW:
    recallHistory(-1);
    break;
  case ABCUI_KEY_DOWNARROW:
    recallHistory(1);
    break;
  default:
    break;
  }
}

void onOwnershipLost(void *) {
  if (InputMode != Mode::kNone)
    closeInput();
}

void openInput(Mode mode) {
  AbcUiDocument *doc = panelDocument();
  if (!doc || mode == Mode::kNone)
    return;
  InputMode = mode;
  setPrompt(mode);
  setInputText(std::string());
  setProperty(query("chat-input-row"), "display", "block");
  // The scrollbar needs the pointer while the reader browses the log.
  Api->ui->documentSetProperty(doc, "pointer-events", "auto");
  setVisible(true);
  if (AbcUiElement *input = inputElement())
    Api->ui->elementFocus(input);
  if (!Token) {
    AbcUiInputCallbacks callbacks{};
    callbacks.structSize = sizeof(callbacks);
    callbacks.userdata = nullptr;
    callbacks.claimsKey = &claimsKey;
    callbacks.claimsWheel = &claimsWheel;
    callbacks.blocksGameplay = &blocksGameplay;
    callbacks.onClaimedKeyDown = &onClaimedKeyDown;
    callbacks.onClaimedWheelPulse = &onWheelPulse;
    callbacks.onOwnershipLost = &onOwnershipLost;
    Token = Api->input->takeToken(&callbacks);
  }
  HideAt = Clock + sayTextTime();
  FadeOpacity = -1;
  ScrollFrames = 2;
  HistoryCursor = -1;
  HistoryDraft.clear();
  updateImeBadge();
  updateFade();
  updateVisibility();
}

void closeInput() {
  if (InputMode == Mode::kNone && !Token) {
    setProperty(query("chat-input-row"), "display", "none");
    return;
  }
  InputMode = Mode::kNone;
  // Drop a live IME composition: its commit must not land in a closed
  // editor.
  Api->input->cancelComposition();
  setProperty(query("chat-input-row"), "display", "none");
  setInputText(std::string());
  if (AbcUiElement *input = inputElement())
    Api->ui->elementBlur(input);
  if (Token) {
    Api->input->releaseToken(Token);
    Token = 0;
  }
  if (AbcUiDocument *doc = panelDocument()) {
    Api->ui->documentSetProperty(doc, "pointer-events", "none");
    // Linger for one full window after the input closes, then fade.
    HideAt = Clock + sayTextTime();
    FadeOpacity = -1;
    updateFade();
    updateVisibility();
  }
}

// ---- event sinks ----

int32_t onSayText(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiSayTextEvent *>(payload);
  if (!chatVisible())
    return 0; // stay out of the way: the native handler keeps drawing
  // Voice-muted senders are dropped, but the line is still consumed so the
  // native log cannot show it either.
  if (cvarInt(SayTextBlockMuted, 0) != 0 && event->client > 0 &&
      Api->voice->isBlocked(event->client))
    return 1;

  AbcUiPlayerInfo sender{};
  sender.structSize = sizeof(sender);
  // A non-positive client byte is the server console's own line (`say` typed
  // into the dedicated-server console arrives as SayText client 0 or
  // TextMsg/HUD_PRINTTALK): no sender row, no player tint.
  const bool known = event->client >= 1 && event->client <= 32 &&
                     Api->players->get(event->client, &sender) && sender.valid;
  const std::string senderName = known ? sender.name : std::string();
  const std::vector<chatsaytext::Run> runs = chatsaytext::buildRuns(
      event->text ? event->text : "", event->params, senderName);

  // The console mirror the native handler did.
  std::string plain;
  for (const chatsaytext::Run &run : runs)
    plain += run.text;
  if (!plain.empty())
    Api->core->print("%s", plain.c_str());
  if (!runs.empty())
    appendNode(runs, playerColorCss(event->client));
  return 1;
}

// ---- commands ----

int32_t onMessageMode(void *userdata, const void *) {
  const bool team = reinterpret_cast<intptr_t>(userdata) != 0;
  // A bare invocation opens the Rml input; an argumented one ("say text")
  // keeps the engine's own path. The command bridge does not carry argv, so
  // an argumented invocation is indistinguishable here - consumed like the
  // bare one (documented deviation: "messagemode hello" now opens the line
  // with the text instead of sending it directly).
  if (!chatEnabled())
    return 0;
  openInput(team ? Mode::kTeam : Mode::kSay);
  return 1;
}

// ---- panel callbacks ----

void panelReset(void *) {
  clearLines();
  closeInput();
}

void panelOnData(void *, double frameTime) {
  Clock += frameTime;
  applyWindowHeight();
  // The pause menu (or HIDEALL) takes the keyboard: never leave a focused
  // editor behind another surface.
  if (InputMode != Mode::kNone) {
    AbcUiHudState state{};
    state.structSize = sizeof(state);
    Api->data->hudState(&state);
    if (Api->input->menuUp() || (state.hideBits & ABCUI_HIDEBIT_ALL))
      closeInput();
  }
  // The appended row is measured by the next layout pass.
  if (ScrollFrames > 0) {
    scrollLogToBottom();
    ScrollFrames--;
  }
  if (InputMode != Mode::kNone)
    updateImeBadge();
  updateFade();
  updateVisibility();
}

// ---- lifecycle ----

void onModuleFrame(double) {
  Ime.frame();
  maintainChatOwnership();
}

void onModuleLoad() {
  Ime.load(Api, "abcui.chat");
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

  Api->events->subscribeVeto(ABCUI_CHANNEL_TEXTFEED_SAY_TEXT, 0, &onSayText,
                             nullptr);
  Api->commands->subscribeCommand("messagemode", 0, &onMessageMode, nullptr);
  Api->commands->subscribeCommand("messagemode2", 0, &onMessageMode,
                                  reinterpret_cast<void *>(1));
}

void onModuleInit() {
  ChatMode = Api->console->registerCvar(kChatMode, "1", ABCUI_CVAR_VALUE);
  // Read-only mirrors of the client's cvars.
  SayTextEnabled = Api->console->findCvar("hud_saytext");
  SayTextTime = Api->console->findCvar("hud_saytext_time");
  SayTextLines = Api->console->findCvar("hud_saytext_lines");
  SayTextBlockMuted = Api->console->findCvar("hud_saytext_block_muted");
  CaptionModChat = Api->console->findCvar("cap_newchat");
  maintainChatOwnership();
}

void onModuleShutdown() {
  Ime.reset();
  // The document dies with the context; drop the token and the rows.
  closeInput();
  Lines.clear();
  HideAt = 0.0;
  FadeOpacity = -1;
  WindowRows = 0;
  ImeBadge.clear();
}

void onModuleUnload() {
  Ime.unload();
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
      !api->events || !api->ui || !api->console || !api->data ||
      !api->documents || !api->players || !api->input || !api->voice ||
      !api->commands)
    return 0;
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->console->structSize < sizeof(AbcUiConsoleApi) ||
      api->data->structSize < sizeof(AbcUiDataApi) ||
      api->players->structSize < sizeof(AbcUiPlayersApi) ||
      api->input->structSize < sizeof(AbcUiInputApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.chat";
  out->version = "2.1.0";
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = &onModuleFrame;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
