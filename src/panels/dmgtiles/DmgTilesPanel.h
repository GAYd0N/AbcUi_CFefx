#pragma once

// panels::DmgTiles - damage-type tile stack (poison/acid/freeze/...).
// from: ABCEnchance/src/hud/vgui/dmgtiles.* (GBK->UTF-8).
//
// Old control mapping: 10 CTileIconItems become 10 tile divs in a flex column
// packed to the bottom (old PaintBackground bottom-up layout = the 08 §4.24
// column + justify-content flex-end rule); the old HudAnimations.txt ramp
// (alpha 0 -> 255 in 0.1s, hold 3.0s, -> 0 in 0.1s) is reproduced with
// timestamps in onData. Inactive tiles use display:none (the old code only
// positioned visible tiles; hidden ones must not take flex space).

#include "../../framework/HudPanel.h"

namespace panels {

class DmgTilesPanel : public framework::HudPanel {
public:
  DmgTilesPanel();

  void reset() override;

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;

private:
  // Old DMG_TYPE bits per tile, ctor order (Poison..Sonic). The enum values
  // are the wire contract (UpdateTiles bits) - never renumber.
  static constexpr int kTileBits[10] = {
      1 << 17,              // poison  DMG_POISON
      1 << 20,              // acid    DMG_ACID
      (1 << 4) | (1 << 22), // freeze  DMG_FREEZE | DMG_SLOWFREEZE
      1 << 14,              // drown   DMG_DROWN
      (1 << 3) | (1 << 21), // burn    DMG_BURN | DMG_SLOWBURN
      1 << 16,              // gas     DMG_NERVEGAS
      1 << 18,              // radiation DMG_RADIATION
      (1 << 8) | (1 << 27), // shock   DMG_SHOCK | DMG_SHOCK_GLOW
      1 << 10,              // energybeam DMG_ENERGYBEAM
      1 << 9,               // sonic   DMG_SONIC
  };
  static constexpr const char *kTileIds[10] = {
      "tile-poison", "tile-acid",  "tile-freeze",    "tile-drown",
      "tile-burn",   "tile-gas",   "tile-radiation", "tile-shock",
      "tile-beam",   "tile-sonic",
  };

  struct TileState {
    double showTime = -1.0; // <0 = inactive
    bool visible = false;
  };

  void updateTiles(int bitsDamage);

  TileState Tiles[10];
  double ClientTimeNow = 0.0;
};

} // namespace panels
