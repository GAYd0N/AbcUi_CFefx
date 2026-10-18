#pragma once

// panels::Motd - the mission briefing / server message window (old
// CMotdPanel, from: ABCEnchance/src/hud/vgui/motd.*, GBK->UTF-8).
//
// D-17 deviations: RmlUi has no HTML control, so HTML MOTDs render as their
// stripped text content and http(s) MOTD URLs render as text with a console
// note (the old vgui HTML control embedded a browser; auto-opening server
// URLs in the OS browser would be an untrusted-content hole). The old
// character-count pagination rode vgui font metrics - one scrollable page
// with the same keep_time/fade rhythm replaces it.

#include "../../framework/HudPanel.h"

namespace panels {

namespace motd {
// HUD_Init-time cvar registration (plan 08 §4.9).
void registerCvars();
// Wheel ownership: the open motd scrolls (pagination replacement, D-17).
bool wantsWheel();
// The client's ShowVGUIMenu(MOTD) must stay down while hud_motd is non-zero:
// >= 1 the new panel replaces it, < 0 suppresses both, 0 restores vanilla
// (the old TFV_ShowVGUIMenu hook's gate).
bool blocksVanillaWindow();
// The hook's entry into the panel (old CViewport::ShowMOTD): shows the motd
// under the hud_motd >= 1 gate. Null-panel safe (blocks silently, like the
// old viewport call into a not-yet-built page).
void showMotd();
} // namespace motd

class MotdPanel : public framework::HudPanel {
public:
  MotdPanel();
  ~MotdPanel() override;

  void reset() override;

  // Old ShowMotd: the missionbriefing command chain and the
  // ShowVGUIMenu(MOTD) hook both land here (hud_motd >= 1 gated).
  void showMotd();

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;

private:
  // Old AppendMotd / FinishSendMOTD (build only - the old finish never
  // showed the window; display waits for missionbriefing).
  void append(const char *message);
  void finishSend();
  void loadMissionBrief();
  void startCountdown();

  std::string Buffer;
  std::string Page;
  bool LoadedMissionBrief = false;
  bool HasContent = false;
  double ShowTime = 0.0;
  double KeepTime = 5.0;
  double FadeTime = 1.0;
  double ClientTimeNow = 0.0;
  bool Fading = false;
};

} // namespace panels
