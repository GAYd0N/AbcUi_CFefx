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
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

const AbcUiModuleApi *Api = nullptr;
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

// Persisted contract (never rename): abc_ui_chat is AbcUi's own switch; the
// hud_saytext_* set belongs to the game client and is only read.
constexpr const char *kChatMode = "abc_ui_chat";

AbcUiCvarHandle ChatMode = nullptr;
AbcUiCvarHandle SayTextEnabled = nullptr;
AbcUiCvarHandle SayTextTime = nullptr;
AbcUiCvarHandle SayTextLines = nullptr;
AbcUiCvarHandle SayTextBlockMuted = nullptr;
AbcUiCvarHandle CaptionModChat = nullptr;

enum class Mode { kNone = 0, kSay, kTeam };
enum class RunKind { kText = 0, kName, kPrefix, kLocation };

struct Run {
  std::string text;
  RunKind kind = RunKind::kText;
};

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

bool otherOwnerActive() {
  // CaptionMod ships cap_newchat (default 1) as its chat replacement switch.
  return CaptionModChat && cvarInt(CaptionModChat, 0) != 0;
}

bool chatEnabled() {
  return cvarInt(ChatMode, 1) > 0 && !otherOwnerActive();
}

bool chatVisible() {
  return chatEnabled() && cvarInt(SayTextEnabled, 1) > 0;
}

double sayTextTime() {
  const float value = cvarFloat(SayTextTime, 0.0f);
  return value > 0.0f ? static_cast<double>(value) : kDefaultSayTextTime;
}

int sayTextLineCount() {
  return std::clamp(cvarInt(SayTextLines, kDefaultSayTextLines), 1, 64);
}

// ---- SayText parsing (the old ChatMessage helper) ----

void appendRun(std::vector<Run> &runs, RunKind kind, const std::string &text) {
  if (text.empty())
    return;
  if (!runs.empty() && runs.back().kind == kind) {
    runs.back().text += text;
    return;
  }
  runs.push_back(Run{text, kind});
}

std::string substituteParams(const std::string &format,
                             const char *const params[4]) {
  bool any = false;
  for (int i = 0; i < 4; i++) {
    if (params[i] && params[i][0])
      any = true;
  }
  if (!any)
    return format;
  std::string out;
  out.reserve(format.size());
  int next = 0;
  for (size_t i = 0; i < format.size(); i++) {
    const char c = format[i];
    if (c != '%' || i + 1 >= format.size()) {
      out += c;
      continue;
    }
    const char spec = format[i + 1];
    if (spec == '%') {
      out += '%';
      i++;
      continue;
    }
    if (spec == 's') {
      if (i + 2 < format.size() && format[i + 2] >= '1' &&
          format[i + 2] <= '9') {
        const int position = format[i + 2] - '1';
        if (position < 4) {
          out += params[position] ? params[position] : "";
          i += 2;
          continue;
        }
        out += c;
        continue;
      }
      if (next < 4)
        out += params[next] ? params[next++] : (next++, "");
      i++;
      continue;
    }
    out += c;
  }
  return out;
}

// Splits a leading "(TEAM) " / "*DEAD* " decoration off the first text run.
void splitPrefix(std::vector<Run> &runs) {
  static const char *const kPrefixes[] = {"(TEAM) ", "*DEAD* ", "*SPEC* ",
                                          "*SPECTATOR* "};
  if (runs.empty() || runs.front().kind != RunKind::kText)
    return;
  for (const char *prefix : kPrefixes) {
    const size_t length = std::strlen(prefix);
    if (runs.front().text.compare(0, length, prefix) != 0)
      continue;
    Run head{prefix, RunKind::kPrefix};
    runs.front().text.erase(0, length);
    if (runs.front().text.empty())
      runs.erase(runs.begin());
    runs.insert(runs.begin(), std::move(head));
    return;
  }
}

std::vector<Run> buildRuns(const std::string &text, const char *const params[4],
                           const std::string &senderName) {
  std::string body = substituteParams(text, params);
  while (!body.empty() && (body.back() == '\n' || body.back() == '\r'))
    body.pop_back();

  std::vector<Run> runs;
  RunKind kind = RunKind::kText;
  std::string pending;
  for (size_t i = 0; i < body.size();) {
    const unsigned char byte = static_cast<unsigned char>(body[i]);
    if (byte == 0x01 || byte == 0x03 || byte == 0x04) {
      appendRun(runs, kind, pending);
      pending.clear();
      kind = byte == 0x01   ? RunKind::kText
             : byte == 0x03 ? RunKind::kName
                            : RunKind::kLocation;
      i++;
      continue;
    }
    if (byte == 0x02) {
      // Legacy marker: the sender name sits inside the text.
      appendRun(runs, kind, pending);
      pending.clear();
      i++;
      const size_t at =
          senderName.empty() ? std::string::npos : body.find(senderName, i);
      if (at == std::string::npos)
        continue;
      appendRun(runs, RunKind::kText, body.substr(i, at - i));
      appendRun(runs, RunKind::kName, senderName);
      i = at + senderName.size();
      continue;
    }
    if (byte < 0x20 || byte == 0x7f) {
      i++;
      continue;
    }
    pending += static_cast<char>(byte);
    i++;
  }
  appendRun(runs, kind, pending);
  splitPrefix(runs);
  return runs;
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

std::string runsToRml(const std::vector<Run> &runs,
                      const std::string &nameColor) {
  std::string rml;
  rml.reserve(256);
  for (const Run &run : runs) {
    switch (run.kind) {
    case RunKind::kName:
      rml += "<span style=\"color: " + nameColor + ";\">" +
             escapeText(run.text) + "</span>";
      break;
    case RunKind::kPrefix:
    case RunKind::kLocation:
      rml += std::string("<span style=\"color: ") + kAccentInk + ";\">" +
             escapeText(run.text) + "</span>";
      break;
    case RunKind::kText:
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
  char value[8] = {};
  std::snprintf(value, sizeof(value), "%.2f", percent / 100.0);
  Api->ui->documentSetProperty(doc, "opacity", value);
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
    char value[16] = {};
    std::snprintf(value, sizeof(value), "%.0fdp", rows * kRowHeightDp);
    setProperty(log, "max-height", value);
  }
}

void appendNode(const std::vector<Run> &runs, const std::string &nameColor) {
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
    char out[128];
    const char *token = mode == Mode::kTeam ? "Chat_Team" : "Chat_Say";
    if (!Api->ui->translate(token, out, sizeof(out)))
      std::snprintf(out, sizeof(out), "%s",
                    mode == Mode::kTeam ? "#Chat_Team" : "#Chat_Say");
    Api->ui->elementSetInnerRML(prompt, escapeText(out).c_str());
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
  const bool known = Api->players->get(event->client, &sender) && sender.valid;
  const std::string senderName = known ? sender.name : std::string();
  const std::vector<Run> runs =
      buildRuns(event->text ? event->text : "", event->params, senderName);

  // The console mirror the native handler did.
  std::string plain;
  for (const Run &run : runs)
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
}

void onModuleShutdown() {
  // The document dies with the context; drop the token and the rows.
  closeInput();
  Lines.clear();
  HideAt = 0.0;
  FadeOpacity = -1;
  WindowRows = 0;
  ImeBadge.clear();
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
      !api->players || !api->input || !api->voice || !api->commands)
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
