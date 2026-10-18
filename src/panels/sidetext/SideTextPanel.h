#pragma once

// panels::SideTextPanel - left-side speed/score readout over the ecco
// background. Ported from ABCEnchance src/hud/vgui/sidetext.* (old
// SideTextPanel.res: 0/c-64 anchor, 72x24, template_text "${SCORE}").
// The template lives in the document text; {SCORE}/{MAXSPEED}/{SPEED}
// placeholders substitute per frame. Score arrives via the ScoreInfo feed
// (the old code read gPlayerRes local frags - PlayerResource lands with
// batch C, the panel keeps its own local-frags copy meanwhile).

#include "../../framework/HudPanel.h"

namespace panels {

namespace sidetext {
// HUD_Init-time cvar registration (plan 08 §4.9).
void registerCvars();
} // namespace sidetext

class SideTextPanel : public framework::HudPanel {
public:
  SideTextPanel();

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;
  void reset() override;

private:
  void updateVisibility();

  std::string Template;
  std::string LastText;
  float MaxSpeed = 0.0f;
  int LocalFrags = 0;
  bool TemplateLoaded = false;
};

} // namespace panels
