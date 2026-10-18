#pragma once

// panels::Indicator - directional hit indicator (pain arc).
// from: ABCEnchance/src/hud/vgui/indicator.* (GBK->UTF-8).
//
// Old control mapping: 4 cycling IndicatorImagePanels become 4 full-screen
// "rotor" divs whose transform rotates the pain_indicator img around the
// screen center (the old Paint math pivoted on the image panel's top-left,
// which coincides with the screen center). The old AnimationController alpha
// ramp (255 for keep_time, then linear fade over fade_time) is reproduced
// with timestamps in onData (same instrument as DeadMsgPanel).
//
// Port note: the old panel used gPlayerRes.IsInSpectate (PlayerResource batch
// C data); the port reuses hudstate::isSpectate (iuser1), the local-player
// equivalent the old Viewport.SetSpectate already keyed on.

#include "../../framework/HudPanel.h"

namespace panels {

namespace indicator {
// HUD_Init-time cvar registration (plan 08 §4.9).
void registerCvars();
} // namespace indicator

class IndicatorPanel : public framework::HudPanel {
public:
  IndicatorPanel();

  void reset() override;

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;

private:
  static constexpr int kSlots = 4;

  struct Slot {
    double startTime = -1.0; // <0 = inactive (old m_flAnimationStartTime 0)
    float hitFrom[3] = {};
    Rml::String appliedAngle;
  };

  void onHit(const float from[3]);

  Slot Slots[kSlots];
  int NextSlot = 0;
  double ClientTimeNow = 0.0;
};

} // namespace panels
