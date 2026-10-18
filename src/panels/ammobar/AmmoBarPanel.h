#pragma once

// panels::AmmoBarPanel - current weapon ammo readout.
// Ported from ABCEnchance src/hud/vgui/ammobar.*; the layout is the
// HL2-inspired pool readout (large clip + small reserve per ammo type, see
// ammobar.rcss), the old AmmoPanel.res anchors are gone.
// Visibility: suit, weapons hide bit clear, weapon present and carrying at
// least one ammo type (old RefreshAmmo rules).

#include "../../data/WeaponResource.h"
#include "../../framework/HudPanel.h"

#include <string>

namespace panels {

class AmmoBarPanel : public framework::HudPanel {
public:
  AmmoBarPanel();

protected:
  void onDocumentReady() override;
  void reset() override;

private:
  void updateVisibility();
  void applyHideToken();
  void refreshAmmo();
  // One ammo pool = one inline line: sprite icon, large clip value and the
  // optional "/reserve" step-down (ammobar.rcss).
  void setPoolVisible(int pool, bool visible);
  void updatePool(int pool, const weaponres::Weapon::SpriteSource &sprite,
                  const std::string &clip, const std::string &reserve);
  const weaponres::Weapon *HandledWeapon = nullptr;
  // Last composed pool markup, so an unchanged AmmoX does not re-parse.
  std::string PoolRml[2];
};

} // namespace panels
