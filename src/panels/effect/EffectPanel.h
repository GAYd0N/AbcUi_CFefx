#pragma once

// panels::Effect - screen effects: danger vignette + water overlays.
// from: ABCEnchance/src/hud/vgui/effect.* (GBK->UTF-8).
//
// Old control mapping: the two ImagePanel overlays (Danger/UnderWater, both
// efx_darkround.png tinted) become two full-screen imgs; the two ImageSprPanel
// water splashes (face_rainleft/right.spr, kRenderTransAdd, framerate 10)
// become two <animimg> elements playing animated WebP files of the same
// frames at the same rate (see rmlui/animated/ElementAnimatedImage.h). The
// element owns the frame clock and the texture; this controller only restarts
// the sequence on the OUT_WATER edge and drives the alpha ramp.
//
// The old AnimationController alpha ramps are reproduced with timestamps in
// onData (DeadMsg instrument): the danger vignette ramps toward its target on
// every Health message; the splash alpha restarts at 255 on every OUT_WATER
// transition, so a per-frame opacity write replaces CSS transitions there.

#include "../../framework/HudPanel.h"

namespace rmlui::animated {
class ElementAnimatedImage;
}

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
  // Typed handle on one splash <animimg> (nullptr when the document element
  // is missing or not an animated image).
  rmlui::animated::ElementAnimatedImage *splash(const char *elementId) const;

  int OldWaterStatus = 0;
  WaterType NowWaterType = WaterType::None;
  double OutWaterStart = 0.0;
  // Client clock for the alpha ramps (the frame clock lives in the element).
  double ClientTimeNow = 0.0;
  // The splash elements replay from frame 0 once per OUT_WATER transition -
  // the old CSPRImage SetFrame(0) restart.
  rmlui::animated::ElementAnimatedImage *LeftSplash = nullptr;
  rmlui::animated::ElementAnimatedImage *RightSplash = nullptr;
  bool OutWaterStarted = false;
};

} // namespace panels
