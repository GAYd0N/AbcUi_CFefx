#pragma once

// panels::Effect - screen effects: danger vignette + water overlays.
// from: ABCEnchance/src/hud/vgui/effect.* (GBK->UTF-8).
//
// Old control mapping: the two ImagePanel overlays (Danger/UnderWater, both
// efx_darkround.png tinted) become two full-screen imgs; the two ImageSprPanel
// water splashes (face_rainleft/right.spr, kRenderTransAdd, framerate 10)
// become imgs whose src flips across sprframe:// URIs (SpriteResource decodes
// the standalone .spr frames; frames upload lazily).
//
// The old AnimationController alpha ramps are reproduced with timestamps in
// onData (DeadMsg instrument): the danger vignette ramps toward its target on
// every Health message; the splash alpha restarts at 255 on every OUT_WATER
// transition, so a per-frame opacity write replaces CSS transitions there.

#include "../../framework/HudPanel.h"

namespace panels {

namespace effect {
// HUD_Init-time cvar registration (plan 08 §4.9).
void registerCvars();
} // namespace effect

class EffectPanel : public framework::HudPanel {
public:
  EffectPanel();

  void reset() override;

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;

private:
  // Old CEffectPanel::WaterType.
  enum class WaterType {
    None = 0,
    UnderWater = 1,
    OutWater = 12,
  };

  void setHealth(int health);
  void setWater(WaterType type);
  // Old CSPRImage::Animate at framerate 10 (visible splashes only).
  void animateWater();
  void applyWaterFrame(const char *elementId, const char *sprPath, int frame);

  int OldWaterStatus = 0;
  WaterType NowWaterType = WaterType::None;
  double OutWaterStart = 0.0;
  // Animation clock for the splash frame flip (old CSPRImage Animate).
  double ClientTimeNow = 0.0;
  double WaterAnimTime = 0.0;
  // The two splash files carry different frame counts (38 vs 33) and the old
  // CSPRImage pair animated independently - one counter per sprite; a shared
  // modulo on the left file walked the right sprite out of range.
  int WaterFrameLeft = 0;
  int WaterFrameRight = 0;
};

} // namespace panels
