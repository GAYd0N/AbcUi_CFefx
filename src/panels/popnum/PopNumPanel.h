#pragma once

// panels::PopNum - floating world-space numbers (the ABCCustomMsg/POPNUMBER
// consumer; the wire side lives in data::netmsg::PopNum).
// from: ABCEnchance/src/hud/vgui/popnum.* (GBK->UTF-8).
//
// Old control mapping: one vgui panel per number becomes one child div of the
// popnum document (DeadMsg row pattern). The old per-frame motion -
// y = proj.y - tall * (-sin(age)/2), i.e. a vertical bob - and the 2-per-frame
// alpha decay both carry over verbatim (old OnThink); geometry is scaled by
// the HUD density exactly like the old proportional panel's GetWide/GetTall.

#include "../../framework/HudPanel.h"

namespace panels {

namespace popnum {
// HUD_Init-time cvar registration (plan 08 §4.9; the old hud_popnumber lived
// in the Viewport ctor).
void registerCvars();
} // namespace popnum

class PopNumPanel : public framework::HudPanel {
public:
  PopNumPanel();

  void reset() override;

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;
  void onRedraw(double frameTime) override;

private:
  static constexpr float kBoxWidth = 48.0f;  // old PopNumberPanel.res wide
  static constexpr float kBoxHeight = 64.0f; // old PopNumberPanel.res tall

  struct Number {
    float origin[3] = {};
    double createTime = 0.0;
    float alpha = 255.0f; // fades over kNumberLifetime (D-17: time-based)
    Rml::Element *element = nullptr;
  };

  void addNumber(const float origin[3], int value, const int color[4]);

  // Old localization tokens (PopNumber_O/K/M/B) with the %s1 slot.
  static std::string formatValue(int value);

  std::vector<Number> Numbers;
  double ClientTimeNow = 0.0;
};

} // namespace panels
