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
// Ownership (T06): while the input line is open the panel holds an
// interactive-HUD router token, so every key belongs to the editor and the
// movement bindings stay silent; the log on its own never owns input. The
// document is pointer-transparent (HudPanel default), exactly like the other
// HUD panels - the input is keyboard-only, as the native one was.
//
// Coexistence with CaptionMod (issue 13): both plugins want the same two
// commands and the same SayText message. Rule - whoever owns chat owns BOTH
// halves. CaptionMod owns it while its `cap_newchat` is on (its hooks
// swallow messagemode before our chain link runs and swallow SayText
// without chaining), so AbcUi's panel stays passive and both of its hooks
// chain through; `cap_newchat 0` hands chat to AbcUi. `abc_ui_chat 0` turns
// the AbcUi side off and leaves the native chat in charge.

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

  // One log row: the moment it starts fading out plus the element the panel
  // owns (dropped together with the entry). The runs themselves live in the
  // DOM - the row is never rebuilt, only appended/removed.
  struct Line {
    double expireAt = 0.0;
    // Quantised fade opacity (0..100) of the row element; -1 forces the
    // first write on the frame the fade starts.
    int opacity = -1;
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
  // Push/pop the send history into the editor (Up/Down).
  void recallHistory(int step);

  void pushLine(const std::string &text, const std::string (&params)[4],
                int client);
  void appendNode(const std::vector<chat::Run> &runs,
                  const std::string &nameColor, double now);
  void expireLines(double now);
  void clearLines();
  void updateVisibility();

  // Input gates: the cvar pair plus the shared HUD visibility token.
  bool chatEnabled() const;
  bool chatVisible() const;

  Rml::Element *inputElement() const;
  std::string inputText() const;
  void setInputText(const std::string &text);

  // framework::uiinput::Owner: the open input line owns every key edge and
  // the wheel (so typing never switches weapons).
  bool claimsKey(int down, int keynum) override;
  bool claimsWheel() override;
  void onClaimedKeyDown(int keynum) override;
  void onOwnershipLost() override;

  Mode Mode_ = Mode::kNone;
  std::vector<Line> Lines;
  std::vector<std::string> History;
  int HistoryCursor = -1;
  std::string HistoryDraft;
  framework::uiinput::Token Token_;
  // HUD_Frame delta accumulation (the panels' client clock; never the
  // engine time, which keeps running while paused).
  double Clock = 0.0;
};

} // namespace panels
