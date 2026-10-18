#pragma once

// panels::FlashlightPanel - flashlight icon/battery readout. Ported from
// ABCEnchance src/hud/vgui/flashlight.* (old FlashLightPanel.res layout).
// Visibility: suit possessed and the flashlight hide bit clear (old
// hudhideCallback + the suit gate in its UpdateClientData handler).

#include "../../framework/HudPanel.h"

namespace panels {

class FlashlightPanel : public framework::HudPanel {
public:
  FlashlightPanel();

protected:
  void onDocumentReady() override;
  void reset() override;

private:
  void updateVisibility();
  void applyHideToken();
  void setFlashLight(bool on, int battery);
  void setFlashBattery(int battery);
  // Lit / off readout brightness (--abc-hud-flashlight-opacity).
  void applyBrightness(bool on);
};

} // namespace panels
