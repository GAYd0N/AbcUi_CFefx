#pragma once

// panels::PlayerBoard - the overhead name/health/armor tile for teammates
// (old CPlayerInfoPanel, from: ABCEnchance/src/hud/vgui/playerboard.*,
// GBK->UTF-8). World-anchored: onRedraw projects the target's +45u origin,
// the tile tracks it while the view-gate (cos60 cone, 1024u range, same
// team, alive) holds.

#include "../../framework/HudPanel.h"
#include "../../framework/DomState.h"

namespace panels {

namespace playerboard {
// HUD_Init-time cvar registration (plan 08 §4.9).
void registerCvars();
} // namespace playerboard

class PlayerBoardPanel : public framework::HudPanel {
public:
  PlayerBoardPanel();

  void reset() override;

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;
  void onRedraw(double frameTime) override;

private:
  framework::DomState AppliedDom;
  // Old UpdateClientInfo: name/bars/icons refresh; false hides the tile.
  bool updateClientInfo(Rml::Element *tile, int entindex);
};

} // namespace panels
