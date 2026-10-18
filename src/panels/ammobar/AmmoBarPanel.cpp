// panels::AmmoBarPanel implementation - see AmmoBarPanel.h.
// from: ABCEnchance/src/hud/vgui/ammobar.* (GBK->UTF-8).
#include "AmmoBarPanel.h"

#include <RmlUi/Core/Element.h>

#include <cstdio>
#include <format>

#include "../../data/HudState.h"
#include "../../data/NetworkMessages.h"
#include "../../data/WeaponResource.h"

namespace panels {

namespace {
// Old CAmmoPanel::WEAPONSTATE / ITEM_FLAG bits used by RefreshAmmo.
constexpr int kStateValid = 1 << 0;
constexpr int kStateUzi = 1 << 2;
constexpr int kUziAkimboFlag = 32;
// Primary / secondary ammo type pools (ammobar.rml #ammo1 / #ammo2).
constexpr const char *kPoolIds[2] = {"ammo1", "ammo2"};
} // namespace

AmmoBarPanel::AmmoBarPanel()
    : HudPanel("ammobar", "abc/abcui/hud/rml/ammobar.rml") {}

void AmmoBarPanel::onDocumentReady() {
  subscribe(netmsg::CurWeapon, [this](int state, int id, int, int) {
    if (state > 0) {
      // Old g_EventCurWeapon handler: weapon lookup may miss (server
      // sent no WeaponList yet) - RefreshAmmo then hides the bar.
      HandledWeapon = weaponres::getWeapon(id);
      refreshAmmo();
    }
  });
  subscribe(netmsg::AmmoX, [this](int, int) { refreshAmmo(); });
  subscribe(hudstate::SuitChanged, [this](bool) {
    updateVisibility();
    refreshAmmo();
  });
  subscribe(hudstate::HideTokenChanged, [this](int) { applyHideToken(); });
  subscribe(hudstate::SpectateChanged, [this](bool) { updateVisibility(); });
}

void AmmoBarPanel::reset() {
  // Old Reset: hide + drop the weapon reference.
  hide();
  HandledWeapon = nullptr;
  for (std::string &rml : PoolRml)
    rml.clear();
}

void AmmoBarPanel::updateVisibility() { applyHideToken(); }

void AmmoBarPanel::applyHideToken() {
  // Old hudhideCallback: ShowPanel(!HIDEWEAPONS) while suited; the
  // Viewport-wide HIDEALL bit hides the ammo bar with everything else.
  // RefreshAmmo re-hides when the weapon carries no ammo types.
  const bool visible = hudstate::hasSuit() &&
                       !hudstate::isHudHidden(hudstate::kHideWeapons) &&
                       !hudstate::isHudHidden(hudstate::kHideAll) &&
                       !hudstate::isSpectate() && HandledWeapon;
  if (visible)
    show();
  else
    hide();
}

void AmmoBarPanel::setPoolVisible(int pool, bool visible) {
  Rml::Element *container = element(kPoolIds[pool]);
  if (!container)
    return;
  container->SetProperty("display", visible ? "block" : "none");
  if (!visible) {
    container->SetInnerRML("");
    PoolRml[pool].clear();
  }
}

void AmmoBarPanel::updatePool(int pool,
                              const weaponres::Weapon::SpriteSource &sprite,
                              const std::string &clip,
                              const std::string &reserve) {
  Rml::Element *container = element(kPoolIds[pool]);
  if (!container)
    return;
  // One inline line: sprite + large clip + optional small reserve. Being
  // inline, every part shares the font baseline (ammobar.rcss note).
  std::string rml;
  if (sprite.valid) {
    const wrect_t &rc = sprite.rect;
    rml = std::format("<img class=\"icon\" src=\"spricon://{}/{},{},{},{}\"/>",
                      sprite.atlas, rc.left, rc.top, rc.right, rc.bottom);
  } else {
    // Keep the sprite slot (the CSS box is 20dp regardless of the source) so
    // a weapon without a usable ammo sprite cannot shift the line box.
    rml = "<img class=\"icon\" style=\"visibility:hidden\"/>";
  }
  // Plain type: the pool spans are the readout (ammobar.rcss draws the 1px
  // outline). The additive glyph helper stays available for other readouts.
  rml += std::format("<span class=\"clip\">{}</span>", clip);
  if (!reserve.empty())
    rml += std::format("<span class=\"reserve\">{}</span>", reserve);
  // AmmoX fires on every ammo change; skip the re-parse when the line is
  // already correct (same sprite, same numbers).
  if (rml == PoolRml[pool])
    return;
  PoolRml[pool] = std::move(rml);
  container->SetInnerRML(PoolRml[pool]);
}

void AmmoBarPanel::refreshAmmo() {
  if (!HandledWeapon) {
    updateVisibility();
    return;
  }
  applyHideToken();
  if (!(HandledWeapon->state & kStateValid))
    return;

  const bool pool1 = HandledWeapon->ammoType > 0;
  const bool pool2 = HandledWeapon->ammo2Type > 0;
  setPoolVisible(0, pool1);
  setPoolVisible(1, pool2);

  // Pool 1: the old akimbo case folds both magazines into the clip value
  // ("clip2xclip"); the reserve stays the shared ammo pool.
  if (pool1) {
    const int ammoLeft = weaponres::countAmmo(HandledWeapon->ammoType);
    std::string clip;
    std::string reserve;
    if (HandledWeapon->clip >= 0) {
      if ((HandledWeapon->state & kStateUzi) &&
          (HandledWeapon->flags & kUziAkimboFlag) && HandledWeapon->clip2 >= 0)
        clip = std::format("{}x{}", HandledWeapon->clip2, HandledWeapon->clip);
      else
        clip = std::to_string(HandledWeapon->clip);
      reserve = std::format("/{}", ammoLeft);
    } else {
      clip = std::to_string(ammoLeft);
    }
    updatePool(0, HandledWeapon->ammo, clip, reserve);
  }

  if (pool2) {
    const int ammoLeft2 = weaponres::countAmmo(HandledWeapon->ammo2Type);
    std::string clip;
    std::string reserve;
    if (HandledWeapon->clip2 >= 0) {
      clip = std::to_string(HandledWeapon->clip2);
      reserve = std::format("/{}", ammoLeft2);
    } else {
      clip = std::to_string(ammoLeft2);
    }
    updatePool(1, HandledWeapon->ammo2, clip, reserve);
  }

  // The divider only means something between two live pools.
  if (Rml::Element *separator = element("pool-sep"))
    separator->SetProperty("display", (pool1 && pool2) ? "block" : "none");

  if (!pool1 && !pool2)
    hide();
}

} // namespace panels
