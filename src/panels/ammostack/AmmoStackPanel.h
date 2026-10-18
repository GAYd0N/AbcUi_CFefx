#pragma once

// panels::AmmoStackPanel - ammo pickup stack (icon + count), bottom-left of
// the crosshair area. Ported from ABCEnchance src/hud/vgui/ammostack.*
// (AmmoStackPanel.res: c45/c-128 anchor, 80x200, item 80x20, keep 5s).

#include "../../framework/HudPanel.h"
#include "../../framework/StackBase.h"

namespace panels {

class AmmoStackPanel : public framework::HudPanel, public framework::StackBase {
public:
  AmmoStackPanel();

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;
  void reset() override;
};

} // namespace panels
