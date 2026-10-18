#pragma once

// panels::Radar - the FBO minimap (batch D, plan 05 §7 route 2).
// from: ABCEnchance/src/hud/vgui/radar.* (GBK->UTF-8).
//
// Route 2 (05 §7, recorded in 00 D-19): the Rml document owns the plate art,
// the north/aim marks and the player avatars; the world render goes to a
// fixed 1024² FBO during the engine's pre-view phase (old OnPreRenderView ->
// CRadarPanel::RenderRadar) and the FBO colour texture is handed to Rml as
// an *external masked texture* (AbcRenderInterface::registerExternalTexture):
// the map <img> displays it with zero per-frame uploads (R-11 closed by
// construction - the old per-frame texture path never existed here), and the
// square/round mask rides a live slot read at draw time.
//
// FBO lifecycle: created lazily on the first pre-view callback, recreated on
// the engine's frame-buffer regeneration (OnFreeFrameBuffers ->
// OnGenerateFrameBuffers, the ModelViewPanel pattern), freed by the panel
// destructor. The old radar never registered those callbacks (a latent hole
// on display-mode changes); D-19 closes it.
//
// Geometry parity: the plate is 100 design units at (5,5) (old
// RadarPanel.res; +scale_radar swaps to the 300-unit scaled box), children
// use the dp mapping like every ported panel, while the avatar size and the
// per-frame avatar/north placement stay in raw pixels - exactly the old
// code's mixed-unit behaviour (SetSize(avatarSize) and SetPos(stx, sty) were
// raw px against the proportional panel).

#include "../../framework/HudPanel.h"
#include "../../framework/DomState.h"

namespace panels {

class RadarPanel;

namespace radar {

// Old HUD_Init-time registration (persisted names - never rename).
void registerCvars();
// Old ADD_COMMAND("+scale_radar"/"-scale_radar"): hold to enlarge.
void registerCommands();

// Live panel for the command seams (old GetRadarPanel()).

#ifndef NDEBUG
// abc_ui_probe aid: FBO texture id (0 = not ready).
int debugFboTexture();
#endif

} // namespace radar

class RadarPanel : public framework::HudPanel {
public:
  RadarPanel();
  ~RadarPanel() override;

  void reset() override;

  // Old SetScale (the +/-scale_radar pair): swap the plate box between the
  // base and the scaled size.
  void setScale(bool state);

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;

private:
  framework::DomState AppliedDom;
  std::unordered_map<int, Rml::Element *> AvatarElements;
  bool Scaled = false;
};

} // namespace panels
