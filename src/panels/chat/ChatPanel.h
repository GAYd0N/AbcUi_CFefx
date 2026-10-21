#pragma once

// panels::chat::ChatPanel - the RmlUi chat input and the player chat log
// (issue 13).
//
// Reference behavior: CaptionMod src/chatdialog.cpp - the message-mode input
// line (prompt, Enter submits, Esc cancels, Up/Down walk the send history),
// the fade/line-count rules around the log, and the say/say_team assembly.
// AbcUi keeps the command-hook shape the issue asks for instead of the
// viewport replacement CaptionMod uses: `messagemode`/`messagemode2` are
// chained commands (framework::commands), SayText is chained through
// data::netmsg, and the client viewport is never replaced.
//
// Log model (review feedback): the panel keeps a 256-message ring buffer and
// shows it in a compact scrollable window (at most kMaxWindowRows rows). The
// window auto-hides `hud_saytext_time` after the last line, fades its last
// second out as one block, and keeps every row in the DOM so the scrollbar
// still reaches older lines while the chat is open. Rows are escaped text
// with the HUD plate styling; long lines wrap, they never widen the panel.
//
// Ownership (T06): while the input line is open the panel holds an
// interactive-HUD router token, so every key belongs to the editor and the
// movement bindings stay silent; the log on its own never owns input. The
// document is pointer-transparent while only the log is up - the input is
// keyboard-only, as the native one was - and becomes pointer-capable while
// the input line is open so the scrollbar can be dragged (the router already
// shows the cursor for that owner).
//
// Coexistence with CaptionMod (issue 13): both plugins want the same two
// commands and the same SayText message. Rule - whoever owns chat owns BOTH
// halves. CaptionMod owns it while its `cap_newchat` is on (its hooks
// swallow messagemode before our chain link runs and swallow SayText
// without chaining), so AbcUi's panel stays passive and both of its hooks
// chain through; `cap_newchat 0` hands chat to AbcUi. `abc_ui_chat 0` turns
// the AbcUi side off and leaves the native chat in charge.

#include <deque>
#include <string>
#include <vector>

#include "../../framework/HudPanel.h"
#include "../../framework/UiInputRouter.h"
#include "ChatMessage.h"

struct cvar_s;

namespace Rml {
class Element;
}

namespace netmsg {
struct SayTextData;
}

namespace panels {

namespace chat {
// HUD_Init-time cvar registration (plan 08 §4.9).
void registerCvars();
// True when another plugin already owns the chat (CaptionMod with
// `cap_newchat` non-zero); the panel then stays passive and chains through.
bool otherOwnerActive();
} // namespace chat

class ChatPanel : public framework::HudPanel,
                  private framework::uiinput::Owner {
public:
  ChatPanel();
  ~ChatPanel() override;

  void reset() override;

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;

private:
  enum class Mode { kNone = 0, kSay, kTeam };

  // One buffered row: the element the panel owns. The runs themselves live in
  // the DOM - a row is never rebuilt, only appended and trimmed.
  struct Line {
    Rml::Element *node = nullptr;
  };

  // SayText feed: append the line and swallow the native draw.
  bool onSayText(const netmsg::SayTextData &data);
  // `messagemode` / `messagemode2` chain subscriber.
  bool onMessageMode(Mode mode);

  void openInput(Mode mode);
  void closeInput();
  void submit();
  void setPrompt(Mode mode);
  // Input-method badge at the end of the input row ("CN"/"JP"/...); empty
  // hides it. Re-read every frame while the line is open so switching IMEs
  // while typing updates the badge.
  void updateImeBadge();
  // Push/pop the send history into the editor (Up/Down).
  void recallHistory(int step);

  void pushLine(const std::string &text, const std::string (&params)[4],
                int client);
  void appendNode(const std::vector<chat::Run> &runs,
                  const std::string &nameColor, double now);
  void clearLines();
  void updateVisibility();
  // Block-level fade out of the whole log in its last second
  // (hud_saytext_time), written only when the quantised value changes.
  void updateFade();
  // Applies the window height from hud_saytext_lines (change-gated).
  void applyWindowHeight();
  // Scroll plumbing: the log is the scrolling element.
  Rml::Element *chatLog() const;
  bool atLogBottom() const;
  void scrollLogToBottom();
  void scrollLog(float deltaDp);

  // Input gates: the cvar pair plus the shared HUD visibility token.
  bool chatEnabled() const;
  bool chatVisible() const;

  Rml::Element *inputElement() const;
  std::string inputText() const;
  void setInputText(const std::string &text);

  // framework::uiinput::Owner: the open input line owns every key edge and
  // the wheel (so typing never switches weapons and the wheel scrolls the
  // log instead).
  bool claimsKey(int down, int keynum) override;
  bool claimsWheel() override;
  // The open input line claims the engine's key routing: while this is true
  // the BaseUI key seam reports every claimed edge as handled by the UI (the
  // mechanism the native chat entry and CaptionMod's VGUI dialog use), so the
  // engine never executes a binding - WASD, jump, +use and any other bind.
  bool blocksGameplay() const override;
  bool onClaimedWheelPulse(int keynum) override;
  void onClaimedKeyDown(int keynum) override;
  void onOwnershipLost() override;

  Mode Mode_ = Mode::kNone;
  // Ring buffer of live rows (kMaxBuffer entries, oldest dropped first).
  std::deque<Line> Lines;
  std::vector<std::string> History;
  int HistoryCursor = -1;
  std::string HistoryDraft;
  framework::uiinput::Token Token_;
  // HUD_Frame delta accumulation (the panels' client clock; never the
  // engine time, which keeps running while paused).
  double Clock = 0.0;
  // When the log stops being shown (last line + hud_saytext_time).
  double HideAt = 0.0;
  // Height applied to the log window in rows; 0 = not applied yet.
  int WindowRows = 0;
  // Frames left that still pull the log to the tail. A row's height is only
  // known after the next layout pass, so an append arms a couple of frames.
  int ScrollFrames = 0;
  // Last badge text written to the DOM (change-gated like the other writes).
  std::string ImeBadge;
  // Last block opacity written (percent), -1 = not written yet.
  int FadeOpacity = -1;
};

} // namespace panels
