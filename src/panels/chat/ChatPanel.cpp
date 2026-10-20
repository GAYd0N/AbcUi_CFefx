// panels::chat::ChatPanel implementation - see ChatPanel.h.
//
// from: CaptionMod src/chatdialog.cpp (input line, history, fade, prompt,
// say/say_team assembly) and src/Viewport.cpp (StartMessageMode/ChatPrintf),
// re-shaped onto the AbcUi seams: framework::commands for the message-mode
// verbs, data::netmsg for SayText, the T06 router for keyboard ownership and
// the HUD theme for the look. The client viewport is NOT replaced, so the
// native chat input is suppressed purely by consuming the command.

#include "ChatPanel.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/Traits.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include <abc/Console.h>
#include <abc/CvarCmd.h>
#include <command.h>
#include <keydefs.h>

#include "../../data/HudState.h"
#include "../../data/NetworkMessages.h"
#include "../../framework/Commands.h"
#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"
#include "../../hooks.h"
#include "../../rmlui/InputBridge.h"
#include "../../rmlui/TextInput.h"
#include "../../services/GameUiBridge.h"
#include "../../services/voice/VoiceStatus.h"

namespace panels {

namespace {
// CaptionMod's per-line fade window (chatdialog.h CHATLINE_FADE_TIME); the
// scroll time is the game's own hud_saytext_time.
constexpr double kFadeSeconds = 1.0;
// Used only when the client did not register its saytext cvars (a mod whose
// HUD never created them): CaptionMod's own fallbacks, so the panel still
// behaves like a chat instead of drawing nothing.
constexpr float kDefaultSayTextTime = 10.0f;
constexpr int kDefaultSayTextLines = 5; // HLSDK MAX_LINES
} // namespace

namespace chat {

// Persisted contract (never rename): abc_ui_chat is AbcUi's own switch, the
// hud_saytext_* set belongs to the game client and is only read.
cvar_t *ChatMode = nullptr;
cvar_t *SayTextEnabled = nullptr;
cvar_t *SayTextTime = nullptr;
cvar_t *SayTextLines = nullptr;
cvar_t *SayTextBlockMuted = nullptr;

void registerCvars() {
  // 0 = off (the native chat stays in charge), 1 = on unless CaptionMod's
  // own chat replacement is enabled (see otherOwnerActive).
  ChatMode = abc::CREATE_CVAR("abc_ui_chat", "1", abc::FCVAR_VALUE, nullptr);
  // Read-only mirrors of the client's cvars: registering them here would
  // fight the client's own defaults/archive flags.
  SayTextEnabled = abc::CVAR_GET_POINTER("hud_saytext");
  SayTextTime = abc::CVAR_GET_POINTER("hud_saytext_time");
  SayTextLines = abc::CVAR_GET_POINTER("hud_saytext_lines");
  SayTextBlockMuted = abc::CVAR_GET_POINTER("hud_saytext_block_muted");
}

bool otherOwnerActive() {
  // CaptionMod ships cap_newchat (FCVAR_ARCHIVE, default 1) as its chat
  // replacement switch. While it is on, CaptionMod owns both halves of the
  // feature (its messagemode hook swallows the command before ours runs and
  // its SayText hook returns "handled" without chaining), so pretending to
  // own the log here would only produce an empty chat page. Set
  // `cap_newchat 0` to hand chat to AbcUi; the cvar is absent entirely when
  // CaptionMod is not installed.
  cvar_t *competitor = abc::CVAR_GET_POINTER("cap_newchat");
  return competitor && competitor->value != 0.0f;
}

} // namespace chat

namespace {

double sayTextTime() {
  const float value = chat::SayTextTime ? chat::SayTextTime->value : 0.0f;
  return value > 0.0f ? static_cast<double>(value) : kDefaultSayTextTime;
}

int sayTextLineCount() {
  const int value = chat::SayTextLines
                        ? static_cast<int>(chat::SayTextLines->value)
                        : kDefaultSayTextLines;
  return std::clamp(value, 1, 64);
}

bool blockMutedPlayers() {
  return chat::SayTextBlockMuted && chat::SayTextBlockMuted->value != 0.0f;
}

std::string translated(const char *token) {
  std::string out;
  if (framework::Localization::translate(out, token))
    return out;
  return token;
}

// Engine per-player tint (skills the scoreboard/playerboard already read);
// empty keeps the sheet's name colour.
std::string playerColorCss(int client) {
  if (client <= 0 || !hooks::GetClientColor)
    return {};
  const float *tint = hooks::GetClientColor(client);
  if (!tint)
    return {};
  char buffer[16] = {};
  std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x",
                static_cast<int>(std::clamp(tint[0], 0.0f, 1.0f) * 255.0f),
                static_cast<int>(std::clamp(tint[1], 0.0f, 1.0f) * 255.0f),
                static_cast<int>(std::clamp(tint[2], 0.0f, 1.0f) * 255.0f));
  return buffer;
}

std::string playerName(int client) {
  if (client <= 0)
    return {};
  hud_player_info_t info = {};
  gEngfuncs.pfnGetPlayerInfo(client, &info);
  return info.name ? info.name : std::string();
}

const char *runClass(chat::RunKind kind) {
  switch (kind) {
  case chat::RunKind::kName:
    return "chat-name";
  case chat::RunKind::kPrefix:
    return "chat-prefix";
  case chat::RunKind::kLocation:
    return "chat-location";
  default:
    return "chat-text";
  }
}

// One log row as RML. Every run text goes through the escaper: a server
// line that contains markup must render as text, never as structure
// (the D-17 #3 rule the notice/killfeed panels follow).
std::string runsToRml(const std::vector<chat::Run> &runs,
                      const std::string &nameColor) {
  std::string rml;
  for (const auto &run : runs) {
    rml += "<span class=\"";
    rml += runClass(run.kind);
    rml += "\"";
    if (run.kind == chat::RunKind::kName && !nameColor.empty()) {
      rml += " style=\"color:";
      rml += nameColor;
      rml += "\"";
    }
    rml += ">";
    rml += framework::TextUtil::escapeRml(run.text);
    rml += "</span>";
  }
  return rml;
}

// RmlUi removes a row through its parent (Element has no self-remove).
void removeRow(Rml::Element *node) {
  if (!node)
    return;
  if (Rml::Element *parent = node->GetParentNode())
    parent->RemoveChild(node);
}

// Caret behind the text after a wholesale value replacement (the same helper
// the console page uses: a history recall must leave the editor ready to
// type, not with the caret parked mid-string).
void parkCaretAtEnd(Rml::Element *editor) {
  auto *input = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(editor);
  if (!input)
    return;
  const Rml::String value = input->GetValue();
  const int end = static_cast<int>(Rml::StringUtilities::LengthUTF8(
      Rml::StringView(value.data(), value.data() + value.size())));
  input->SetSelectionRange(end, end);
}

} // namespace

ChatPanel::ChatPanel() : HudPanel("chat", "abc/abcui/hud/rml/chat.rml") {}

ChatPanel::~ChatPanel() {
  // The router must never keep an Owner pointer into a destroyed panel.
  Token_.release();
}

void ChatPanel::onDocumentReady() {
  // SayText is chained through the data layer; the panel is the log.
  subscribe(netmsg::SayText, [this](const netmsg::SayTextData &data) {
    return onSayText(data);
  });
  // Message modes: the bare command opens our input line, "say text" keeps
  // its native meaning (the argc guard chains those through).
  const struct {
    const char *name;
    Mode mode;
  } kMessageModes[] = {{"messagemode", Mode::kSay},
                       {"messagemode2", Mode::kTeam}};
  for (const auto &entry : kMessageModes) {
    if (abc::CChainedCommand *command = commands::get(entry.name)) {
      subscribe(command->Event(),
                [this, mode = entry.mode]() { return onMessageMode(mode); });
    } else {
      abc::ConsoleWarn("[AbcUi] chat: \"%s\" is not on the command bus\n",
                       entry.name);
    }
  }
  // Hide-HUD token: the chat follows the viewport-wide HIDEALL bit like the
  // notice/killfeed panels do.
  subscribe(hudstate::HideTokenChanged, [this](int) { updateVisibility(); });
  if (Rml::Element *row =
          Document ? Document->GetElementById("chat-input-row") : nullptr)
    row->SetProperty("display", "none");
}

void ChatPanel::reset() {
  closeInput();
  clearLines();
  hide();
}

bool ChatPanel::chatEnabled() const {
  if (!chat::ChatMode || chat::ChatMode->value <= 0.0f)
    return false;
  return !chat::otherOwnerActive();
}

bool ChatPanel::chatVisible() const {
  // hud_saytext is the client's "enable text chat" switch: while it is off
  // the native handler draws nothing either, so the message chains through
  // and simply never appears.
  if (chat::SayTextEnabled && chat::SayTextEnabled->value == 0.0f)
    return false;
  return chatEnabled();
}

bool ChatPanel::onMessageMode(Mode mode) {
  // "say hello" / "messagemode <arg>" are not the bare message mode; only
  // the argument-less invocation belongs to the RmlUi input.
  if (gEngfuncs.Cmd_Argc() > 1)
    return true;
  if (!Document || !chatVisible())
    return true; // chain: the native input (or the competing plugin) opens
  openInput(mode);
  return false;
}

bool ChatPanel::onSayText(const netmsg::SayTextData &data) {
  if (!Document)
    return true;
  // Disabled or another plugin owns chat: the native/other log draws it.
  if (!chatVisible())
    return true;
  if (hudstate::isHudHidden(hudstate::kHideAll) && !Token_.held())
    return true;
  // hud_saytext_block_muted: the line is dropped, and swallowed so neither
  // log shows it.
  if (blockMutedPlayers() && data.client > 0 &&
      services::voice::status().isPlayerBlocked(data.client))
    return false;
  pushLine(data.text, data.params, data.client);
  return false;
}

void ChatPanel::pushLine(const std::string &text,
                         const std::string (&params)[4], int client) {
  if (!Document)
    return;
  const std::vector<chat::Run> runs =
      chat::buildRuns(text, params, playerName(client));
  if (runs.empty())
    return;
  // Native parity: the client also mirrors every chat line into the
  // console (CHudSayText::SayTextPrint ends with a ConsolePrint), which is
  // the only place a player can re-read what scrolled away.
  std::string plain;
  for (const auto &run : runs)
    plain += run.text;
  gEngfuncs.pfnConsolePrint(plain.c_str());
  gEngfuncs.pfnConsolePrint("\n");
  appendNode(runs, playerColorCss(client), Clock);
}

void ChatPanel::appendNode(const std::vector<chat::Run> &runs,
                           const std::string &nameColor, double now) {
  Rml::Element *log = Document ? Document->GetElementById("chat-log") : nullptr;
  if (!log)
    return;
  Rml::ElementPtr node = Document->CreateElement("div");
  node->SetClass("chat-line", true);
  node->SetInnerRML(runsToRml(runs, nameColor));
  Rml::Element *row = log->AppendChild(std::move(node));

  Line line;
  // While the input line is open the log is a reader surface: nothing
  // expires until it closes (closeInput re-arms the countdown).
  line.expireAt = now + sayTextTime();
  line.node = row;
  Lines.push_back(std::move(line));

  const int limit = sayTextLineCount();
  while (static_cast<int>(Lines.size()) > limit) {
    if (Lines.front().node)
      removeRow(Lines.front().node);
    Lines.erase(Lines.begin());
  }
  updateVisibility();
}

void ChatPanel::expireLines(double now) {
  if (Token_.held())
    return; // the input line is open: the reader is looking at the log
  for (auto it = Lines.begin(); it != Lines.end();) {
    const double remaining = it->expireAt - now;
    if (remaining <= 0.0) {
      if (it->node)
        removeRow(it->node);
      it = Lines.erase(it);
      continue;
    }
    // Fade the last second out per row (CaptionMod's CHATLINE_FADE_TIME),
    // written only when the quantised value changes - an ungated per-frame
    // property write re-lays-out the document.
    const int opacity =
        remaining < kFadeSeconds
            ? static_cast<int>(std::clamp(remaining / kFadeSeconds, 0.0, 1.0) *
                               100.0)
            : 100;
    if (it->node && opacity != it->opacity) {
      it->opacity = opacity;
      char value[8] = {};
      std::snprintf(value, sizeof(value), "%.2f",
                    static_cast<double>(opacity) / 100.0);
      it->node->SetProperty("opacity", value);
    }
    ++it;
  }
}

void ChatPanel::clearLines() {
  for (auto &line : Lines) {
    if (line.node)
      removeRow(line.node);
  }
  Lines.clear();
}

void ChatPanel::updateVisibility() {
  if (!Document)
    return;
  const bool wanted = chatVisible() && (!Lines.empty() || Mode_ != Mode::kNone);
  if (!wanted) {
    if (isVisible())
      hide();
    return;
  }
  if (!isVisible())
    show();
}

void ChatPanel::openInput(Mode mode) {
  if (!Document || mode == Mode::kNone)
    return;
  Mode_ = mode;
  setPrompt(mode);
  setInputText(std::string());
  if (Rml::Element *row = Document->GetElementById("chat-input-row"))
    row->SetProperty("display", "block");
  show();
  if (Rml::Element *input = inputElement())
    input->Focus();
  if (!Token_.held())
    Token_ = rmlui::uiRouter().acquire(
        this, framework::uiinput::OwnerTier::kInteractiveHud);
  HistoryCursor = -1;
  HistoryDraft.clear();
  updateVisibility();
}

void ChatPanel::closeInput() {
  if (Mode_ == Mode::kNone && !Token_.held()) {
    if (Rml::Element *row =
            Document ? Document->GetElementById("chat-input-row") : nullptr)
      row->SetProperty("display", "none");
    return;
  }
  Mode_ = Mode::kNone;
  // Drop a live IME composition: its commit must not land in a closed
  // editor (the ESC-first rule the console page follows).
  rmlui::cancelComposition();
  if (Rml::Element *row =
          Document ? Document->GetElementById("chat-input-row") : nullptr)
    row->SetProperty("display", "none");
  setInputText(std::string());
  if (Rml::Element *input = inputElement())
    input->Blur();
  Token_.release();
  HistoryCursor = -1;
  HistoryDraft.clear();
  // Rows held open by the input resume their countdown from here.
  for (auto &line : Lines)
    line.expireAt = Clock + sayTextTime();
  updateVisibility();
}

void ChatPanel::setPrompt(Mode mode) {
  if (!Document)
    return;
  Rml::Element *prompt = Document->GetElementById("chat-prompt");
  if (!prompt)
    return;
  prompt->SetInnerRML(framework::TextUtil::escapeRml(
      translated(mode == Mode::kTeam ? "Chat_Team" : "Chat_Say")));
}

void ChatPanel::submit() {
  // The command is assembled HERE and the payload is sanitised first: the
  // engine tokenizer closes the quoted argument on a double quote and
  // refuses control characters, so neither may survive into `say
  // "<text>"`. Everything else (including ';') stays literal inside the
  // quotes.
  std::string text = inputText();
  std::string clean;
  clean.reserve(text.size());
  for (char c : text) {
    const unsigned char byte = static_cast<unsigned char>(c);
    if (byte < 0x20 || byte == 0x7f || c == '"')
      continue;
    clean += c;
  }
  const auto first = clean.find_first_not_of(" \t");
  const auto last = clean.find_last_not_of(" \t");
  clean = first == std::string::npos ? std::string()
                                     : clean.substr(first, last - first + 1);

  if (!clean.empty()) {
    if (History.empty() || History.back() != clean) {
      History.push_back(clean);
      // Bounded session history (a chat client, not a log).
      if (History.size() > 32)
        History.erase(History.begin());
    }
    const std::string command =
        (Mode_ == Mode::kTeam ? "say_team \"" : "say \"") + clean + "\"";
    abc::EngineClientCmd(command.c_str());
  }
  closeInput();
}

void ChatPanel::recallHistory(int step) {
  if (History.empty() || step == 0)
    return;
  if (HistoryCursor < 0) {
    if (step < 0) {
      // Entering the history: the current draft comes back on the way down.
      HistoryDraft = inputText();
      HistoryCursor = static_cast<int>(History.size()) - 1;
    } else {
      return;
    }
  } else {
    HistoryCursor += step;
    if (HistoryCursor >= static_cast<int>(History.size())) {
      HistoryCursor = -1;
      setInputText(HistoryDraft);
      return;
    }
    if (HistoryCursor < 0)
      HistoryCursor = 0;
  }
  setInputText(History[static_cast<std::size_t>(HistoryCursor)]);
}

Rml::Element *ChatPanel::inputElement() const {
  return Document ? Document->GetElementById("chat-input") : nullptr;
}

std::string ChatPanel::inputText() const {
  auto *input =
      rmlui_dynamic_cast<Rml::ElementFormControlInput *>(inputElement());
  return input ? std::string(input->GetValue().c_str()) : std::string();
}

void ChatPanel::setInputText(const std::string &text) {
  auto *input =
      rmlui_dynamic_cast<Rml::ElementFormControlInput *>(inputElement());
  if (!input)
    return;
  input->SetValue(text.c_str());
  parkCaretAtEnd(input);
}

bool ChatPanel::claimsKey(int down, int keynum) {
  (void)down;
  (void)keynum;
  // The open input line owns every edge: movement bindings must stay silent
  // while typing, exactly like the native chat entry.
  return Mode_ != Mode::kNone;
}

bool ChatPanel::claimsWheel() {
  return Mode_ != Mode::kNone;
}

void ChatPanel::onClaimedKeyDown(int keynum) {
  if (Mode_ == Mode::kNone)
    return;
  // The IME owns these while composing (Enter commits, ESC cancels); the
  // Rml feed already skipped them, so the panel must not act a second time.
  if (rmlui::textComposing())
    return;
  switch (keynum) {
  case K_ESCAPE:
    closeInput();
    break;
  case K_ENTER:
  case K_KP_ENTER:
    submit();
    break;
  case K_UPARROW:
    recallHistory(-1);
    break;
  case K_DOWNARROW:
    recallHistory(1);
    break;
  default:
    break;
  }
}

void ChatPanel::onOwnershipLost() {
  // Focus loss / router teardown: the token is already stale, so drop the
  // editor state instead of pretending to still own the keyboard.
  if (Mode_ != Mode::kNone)
    closeInput();
}

void ChatPanel::onData(double frameTime) {
  if (frameTime > 0.0)
    Clock += frameTime;
  // The pause menu (or HIDEALL) takes the keyboard: do not leave a focused
  // editor behind the other surface.
  if (Mode_ != Mode::kNone && (services::nativeUiBridge().menuUp() ||
                               hudstate::isHudHidden(hudstate::kHideAll)))
    closeInput();
  expireLines(Clock);
  updateVisibility();
}

} // namespace panels
