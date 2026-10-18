#pragma once

// panels::WeaponStackPanel - weapon pickup stack (weapon sprite, red tint
// while out of ammo), right edge above the ammo bar. Ported from ABCEnchance
// src/hud/vgui/weaponstack.* (WeaponStackPanel.res: r120/r340 anchor,
// 120x280, item 120x30, keep 5s).

#include "../../framework/HudPanel.h"
#include "../../framework/StackBase.h"

namespace panels {

class WeaponStackPanel : public framework::HudPanel,
                         public framework::StackBase {
public:
  WeaponStackPanel();

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;
  void reset() override;
};

} // namespace panels
