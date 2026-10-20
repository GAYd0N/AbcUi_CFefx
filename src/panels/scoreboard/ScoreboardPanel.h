#pragma once

// panels::Scoreboard - the +showscores table (old CScorePanel, from:
// ABCEnchance/src/hud/vgui/scoreboard.*, GBK->UTF-8; 1397 lines of vgui
// SectionedListPanel machinery becomes one table rebuild per data version).
//
// Interaction model (old KeyInput/EnableMousePointer): +showscores shows the
// table; MOUSE2 while visible unlocks the OS cursor (centered, old
// EnableMousePointer) and Rml receives mouse input through the normal
// InputBridge gate; right-click on a row opens the context menu (steam
// profile / copy actions and, since issue 17, the voice mute toggle - the
// legacy entry died with voice_status, D-04, and is restored over
// services::voice; the friend-icon overlay needs the steamapi backend's
// Friends surface, still deferred).
//
// Ownership (T06): pointer mode is a router token at the interactive-HUD
// tier - the panel implements framework::uiinput::Owner and the router is
// the single ownership truth. MouseMode only mirrors the token for the
// panel's own logic; a force-release (focus loss) re-syncs it.

#include <RmlUi/Core/Vector2.h>

#include "../../framework/HudPanel.h"
#include "../../framework/UiInputRouter.h"

struct cvar_s;

namespace panels {

namespace scoreboard {
// HUD_Init-time cvar registration (plan 08 §4.9); the pointers live in this
// namespace, the panel reads them.
void registerCvars();
// Spine seam: true = the key serves the scoreboard (MOUSE2 pointer toggle)
// and the engine/binding chain must not run it.
bool onKeyEvent(int down, int keynum);
// Wheel ownership: the table scrolls only in pointer mode (old vgui: the
// scrollbar lived under the vgui cursor).
bool wantsWheel();
// Pointer-mode seam (abc_ui_pointer Debug instrument): injected MOUSE2 never
// reaches the client key chain (P6 acceptance finding), so the driver flips
// the mode directly.
void setPointerMode(bool enabled);
// HUD_Redraw intermission flag: shows the table (old SetInterMission ->
// ShowPanel(intermission > 0)) and mutes +/-showscores (old !m_iInterMission
// guards).
void setIntermission(bool intermission);
// Old CViewport::IsInScore - true while the +/-showscores state is held.
// CL_CreateMove turns this into usercmd IN_SCORE, the only reliable way to
// make the server stream every player's ping/loss (issue #9: the engine
// player tables stay 0/0 the whole session otherwise).
bool inScore();
} // namespace scoreboard

// Context-menu intents. The RML data-action attribute still carries the
// integer, but the panel no longer switches on magic numbers.
enum class ScoreboardAction : int {
  kOpenProfile = 0,
  kCopyProfileUrl,
  kCopySteamName,
  kCopyGameName,
  kCopySteamId,
  kCopySteamId64,
  kMute,
};

class ScoreboardPanel : public framework::HudPanel,
                        private framework::uiinput::Owner {
public:
  ScoreboardPanel();
  ~ScoreboardPanel() override;

  void reset() override;

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;

private:
  friend bool scoreboard::onKeyEvent(int down, int keynum);
  friend void scoreboard::setIntermission(bool intermission);
  friend bool scoreboard::wantsWheel();
  friend void scoreboard::setPointerMode(bool enabled);
  friend bool scoreboard::inScore();
  friend class TableListener;
  class TableListener;

  // Old EnableMousePointer / KeyCodeTyped path.
  bool answerKey(int down, int keynum);

  // Old FullUpdate on show; refresh on the 0.5s period (old
  // UPDATE_PERIOD) or a data-version bump.
  void rebuildTable();
  void updateHeader();
  // Change-gated header write (Phase 8): RmlUi SetInnerRML always rebuilds
  // the element's text subtree (no same-string early-out), so an ungated
  // per-frame write re-lays-out the whole document every frame - the P8
  // benchmark measured ~18ms/frame of exactly that with the board up.
  // `slot` addresses the applied-string cache; returns true when written.
  bool setHeaderRml(int slot, const char *id, const std::string &rml);
  void openContextMenu(int clientIndex, const Rml::Vector2f &position);
  bool overMenu(const Rml::Vector2f &position);
  void closeContextMenu();
  void menuAction(ScoreboardAction action);
  void setVisible(bool visible);
  void setMouseMode(bool enabled);

  // framework::uiinput::Owner (interactive-HUD tier): the pointer-mode key
  // contract is answerKey's MouseMode branch. Consulted only while this
  // panel's token is top-of-stack, so keyboard keys keep their play
  // behavior (chat etc.) while the pointer is up.
  bool claimsKey(int down, int keynum) override;
  bool claimsWheel() override;
  // Force-release (focus loss / teardown): re-sync the mirror through
  // setMouseMode(false) - the token is stale, release() would no-op.
  void onOwnershipLost() override;

  std::string ServerName;
  std::string NextMap;
  int TimeEndCount = 0;
  double TimeEndBase = 0.0;
  double LastRefresh = 0.0;
  double ClientTimeNow = 0.0;
  float AppliedRowHeight = -1;
  bool Visible = false;
  bool MouseMode = false;
  bool Intermission = false;
  // Old CViewport::m_bInScore: the +/-showscores latch (intermission does
  // not set it); CL_CreateMove rides it into usercmd IN_SCORE.
  bool InScore = false;
  long DataVersion = 0;
  long AppliedVersion = -1;
  int MenuClient = 0;
  TableListener *Listener = nullptr;
  // Applied-string cache for the header slots (setHeaderRml) and the last
  // centered content height (update's vertical centering).
  std::string HeaderCache[5];
  float CenteredHeight = -1.0f;
  // Router ownership of pointer mode (T06); MouseMode above mirrors it.
  framework::uiinput::Token PointerToken;
};

} // namespace panels
