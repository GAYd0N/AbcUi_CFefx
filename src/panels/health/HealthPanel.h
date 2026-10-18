#pragma once

// panels::HealthPanel - health/armor bars with danger hue shift, longjump
// indicator. Ported from ABCEnchance src/hud/vgui/health.* (old
// HealthPanel.res layout, scheme colours byte-exact).
//
// Visibility rule (old Viewport hudhideCallback + SetSpectate): suit
// possessed, neither of the health/battery hide bits fully covering, and
// not spectating. hud_danger_health / hud_danger_armor keep their old
// names/defaults (persisted contract).

#include "../../framework/HudPanel.h"

// Engine cvar forward declaration (cvardef.h; the typedef lands with
// metahook.h - this header stays includable before it).
struct cvar_s;

namespace panels {

namespace health {
// HUD_Init-time cvar registration (plan 08 §4.9).
void registerCvars();
// Old gCVars.pDangerHealth - shared with the Effect panel's danger vignette
// (one engine cvar, two readers in the old code as well).
extern cvar_s *DangerHealth;
} // namespace health

class HealthPanel : public framework::HudPanel {
public:
  HealthPanel();

protected:
  void onDocumentReady() override;
  void reset() override;

private:
  void updateVisibility();
  void setHealth(int health);
  void setArmor(int armor);
  // Danger hue (amber -> red below the cvar threshold), then one vitals group
  // update: additive value ink, tinted meter fill and icon, meter width.
  Rml::String dangerColor(int value, float dangerValue) const;
  void applyVital(const char *labelId, const char *barId, const char *iconId,
                  int value, float dangerValue);
  void setLongJump(bool longJump);
};

} // namespace panels
