#pragma once

// panels::Crosshair - dynamic crosshair + per-weapon sprite crosshair.
// from: ABCEnchance/src/hud/vgui/crosshair.* (GBK->UTF-8).
//
// Old control mapping: the 5 vgui child panels (center/top/bottom/left/right)
// and their 5 outline siblings become 10 absolutely-positioned divs driven
// per-frame from onData (old OnThink/PaintBackground); the CSPRImage default
// crosshair becomes one <img> whose src flips between the current weapon's
// spralpha:// crops (old kRenderTransTexture draw - NOT the additive flavor).
//
// Geometry is in raw screen pixels (px), exactly like the old SetBounds calls
// on the proportional vgui panel - the cl_crosshair* cvars are pixel values
// and do not ride the HUD dp scale. Port note: the old drift preferred the
// EVVectorScale sig-scan capture (gHookFuncs client hook, AbcUi has no client
// sig infrastructure); the port always uses the old code's own fallback
// (clientdata punchangle). Third-person center projects a direct view trace
// (AbcEssential Efx precedent); the full PlayerTrace module lands in batch D.

#include <RmlUi/Core/Types.h>

#include "../../framework/HudPanel.h"

struct cvar_s;

namespace panels {

namespace crosshair {

// Old CCrosshairPanel ctor default (persisted contract - never rename).
extern cvar_s *Crosshair; // cl_crosshair

void registerCvars();

} // namespace crosshair

class CrosshairPanel : public framework::HudPanel {
public:
  CrosshairPanel();

  void reset() override;

  // Last-applied DOM state per part (the old code rewrote SetBounds/
  // SetBgColor every frame; Rml property sets invalidate style, so the port
  // writes only on change).
  struct AppliedPart {
    Rml::String color;
    float bounds[4] = {};
    bool valid = false;
  };

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;
  void onRedraw(double frameTime) override;

private:
  struct Part {
    const char *core;
    const char *outline;
  };
  // DOM order: center, top, bottom, left, right (old m_aryCrosshair order).
  static constexpr Part Parts[5] = {
      {"core-center", "outline-center"}, {"core-top", "outline-top"},
      {"core-bottom", "outline-bottom"}, {"core-left", "outline-left"},
      {"core-right", "outline-right"},
  };

  // Hides every dynamic part + the spr img (the gate paths where the old
  // PaintBackground/OnThink left everything empty).
  void hideDynamicCrosshair();
  // Old DrawDefaultCrosshair: pick the weapon's sprite crop by zoom/on-target
  // state and center it on the crosshair center (old PaintBackground tail -
  // independent of the dynamic-part throttle).
  void drawDefaultCrosshair();
  // Computes CenterX/CenterY (third-person projection vs screen center).
  void updateCenter();

  float CenterX = 0.0f;
  float CenterY = 0.0f;
  float PreOffset = 0.0f;
  float AnimTime = 0.0f;
  AppliedPart Core[5];
  AppliedPart Outline[5];
  int CurrentWeaponId = 0;
  // Current spr uri (avoids re-assigning src every frame - texture churn).
  Rml::String SprUri;
};

} // namespace panels
