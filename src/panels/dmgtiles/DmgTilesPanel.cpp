// panels::DmgTilesPanel implementation - see DmgTilesPanel.h.
// from: ABCEnchance/src/hud/vgui/dmgtiles.* (GBK->UTF-8).
#include <metahook.h>

#include "DmgTilesPanel.h"

#include <RmlUi/Core/Element.h>

#include "../../data/HudState.h"
#include "../../data/NetworkMessages.h"
#include "../../framework/Localization.h"

namespace panels {

namespace {

// Old HudAnimations.txt DMGTiles_* sequence: alpha 0 -> 255 in 0.1s, hold,
// fade to 0 in 0.1s. D-17 #4: the hold now honors the res "keep_time 5"
// entry the old code shipped but never consumed (the script hardcoded 3.0s
// while the res said 5 - dead config quirk).
constexpr double kFadeIn = 0.1;
constexpr double kHold = 5.0;
constexpr double kFadeOut = 0.1;

// Label element ids (one per tile, tokens resolved at document-ready).
constexpr const char *kLabelIds[10] = {
    "label-poison", "label-acid",  "label-freeze",    "label-drown",
    "label-burn",   "label-gas",   "label-radiation", "label-shock",
    "label-beam",   "label-sonic",
};

} // namespace

DmgTilesPanel::DmgTilesPanel()
    : HudPanel("dmgtiles", "abc/abcui/hud/rml/dmgtiles.rml") {}

void DmgTilesPanel::onDocumentReady() {
  subscribe(netmsg::Damage, [this](const netmsg::DamageData &data) {
    updateTiles(data.tiles);
  });
  subscribe(hudstate::SuitChanged, [this](bool) {
    // Old ShowPanel(hasSuit) rode the weapon-bits change event.
    if (hudstate::hasSuit())
      show();
    else
      hide();
  });

  // Old labels were vgui-localized tokens ("#DMGTiles_Poison"); resolve
  // them once here (raw token fallback = the old missing-entry behavior).
  for (int i = 0; i < 10; i++) {
    Rml::Element *label = Document->GetElementById(kLabelIds[i]);
    if (!label)
      continue;
    const Rml::String token = label->GetInnerRML();
    std::string text;
    if (framework::Localization::translate(text, token))
      label->SetInnerRML(text);
  }
}

void DmgTilesPanel::reset() {
  // Old Reset: hide the panel + all tiles.
  hide();
  for (TileState &tile : Tiles) {
    tile.showTime = -1.0;
    tile.visible = false;
  }
  if (Document) {
    for (const char *id : kTileIds) {
      if (Rml::Element *tile = Document->GetElementById(id))
        tile->SetProperty("display", "none");
    }
  }
}

void DmgTilesPanel::updateTiles(int bitsDamage) {
  for (int i = 0; i < 10; i++) {
    if ((kTileBits[i] & bitsDamage) == 0)
      continue;
    // Old CTileIconItem::Show: visible + restart the animation sequence.
    Tiles[i].showTime = ClientTimeNow;
    Tiles[i].visible = true;
    if (!isVisible())
      show();
  }
}

void DmgTilesPanel::onData(double frameTime) {
  ClientTimeNow += frameTime;
  if (!Document)
    return;

  // Old Viewport-wide visibility (kHideAll token hides everything).
  if (hudstate::isHudHidden(hudstate::kHideAll))
    hide();
  else if (hudstate::hasSuit())
    show();

  for (int i = 0; i < 10; i++) {
    TileState &tile = Tiles[i];
    if (!tile.visible)
      continue;
    Rml::Element *el = Document->GetElementById(kTileIds[i]);
    if (!el)
      continue;

    const double elapsed = ClientTimeNow - tile.showTime;
    if (elapsed < kFadeIn) {
      // Ramp up (old Linear 0.0 -> 0.1 to 255).
      el->SetProperty("display", "block");
      el->SetProperty("opacity", std::to_string(elapsed / kFadeIn));
    } else if (elapsed < kFadeIn + kHold) {
      el->SetProperty("display", "block");
      el->SetProperty("opacity", "1");
    } else if (elapsed < kFadeIn + kHold + kFadeOut) {
      el->SetProperty("display", "block");
      el->SetProperty(
          "opacity",
          std::to_string(1.0 - (elapsed - kFadeIn - kHold) / kFadeOut));
    } else {
      // Old sequence end: the tile stays invisible until the next Show.
      tile.visible = false;
      el->SetProperty("display", "none");
    }
  }
}

} // namespace panels
