#pragma once

// panels::Vote - the yes/no vote callout (old CVotePanel, from:
// ABCEnchance/src/hud/vgui/vote.*, GBK->UTF-8). Driven by the VoteMenu
// usermsg (gated by hud_vote in the netmsg layer); the bound yes/no keys
// (hud_votekey_yes/no, engine keynums) send voteyes/voteno through the
// HUD_Key_Event spine consumption path.

#include "../../framework/HudPanel.h"

struct cvar_s;

namespace panels {

namespace vote {
// HUD_Init-time cvar registration (plan 08 §4.9); also hands the hud_vote
// gate to the netmsg VoteMenu consumer.
void registerCvars();
// Spine seam: true = the key is a vote answer (or the panel wants the
// press) and the engine/binding chain must not run it.
bool onKeyEvent(int down, int keynum);
} // namespace vote

class VotePanel : public framework::HudPanel {
public:
  VotePanel();
  ~VotePanel() override;

  void reset() override;
  void onData(double) override;

protected:
  void onDocumentReady() override;

private:
  friend bool vote::onKeyEvent(int down, int keynum);

  // Old KeyCodeTyped: the bound yes/no key while visible answers + hides.
  bool answerKey(int down, int keynum);
  // Old StartVote.
  void startVote(int type, const std::string &content, const std::string &yes,
                 const std::string &no);
  void setVisible(bool visible);

  cvar_s *HudVote = nullptr;
  cvar_s *KeyYes = nullptr;
  cvar_s *KeyNo = nullptr;
  bool Visible = false;
};

} // namespace panels
