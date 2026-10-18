// panels::WeaponChoose implementation - see WeaponChoosePanel.h.
// from: ABCEnchance/src/hud/vgui/weaponchoose.cpp (GBK->UTF-8).
// Layout numbers from WeaponChoosePanel.res (slot icons 15x15, active items
// 60x20, inactive 20x20, 1dp gaps; close_time 5); scheme WeaponChoose.*
// colors (ActiveColor = HUD accent amber, Inactive = HUD accent @125, empty
// Red variants, ammo bar 80,80,80,125 / 255,255,0,125, bucket 255 255 0 255).
#include <metahook.h>

#include "WeaponChoosePanel.h"

#include <RmlUi/Core/Element.h>

#include <cmath>
#include <format>
#include <string>

#include <abc/CvarCmd.h>

#include "../../data/HudState.h"
#include "../../data/NetworkMessages.h"
#include "../../data/PlayerResource.h"
#include "../../data/WeaponResource.h"
#include "../../framework/PanelManager.h"
#include "../../framework/Commands.h"

// Windows.h min/max macros fight std::min/std::max (old repo precedent).
#undef max
#undef min

namespace panels {

namespace weaponchoose {

namespace {
WeaponChoosePanel *instance() {
  return static_cast<WeaponChoosePanel *>(
      framework::PanelManager::find("weaponchoose"));
}
} // namespace

bool textmenuOpen() {
  return commands::textMenuOpen();
}

bool attackConfirm() {
  WeaponChoosePanel *panel = instance();
  return panel && panel->attackConfirm();
}

// Old ctor default (persisted contract - never rename).
cvar_t *RainbowMenu = nullptr;

void registerCvars() {
  static bool registered = false;
  if (registered)
    return;
  registered = true;
  RainbowMenu =
      abc::CREATE_CVAR("hud_rainbow_menu", "0", abc::FCVAR_VALUE, nullptr);
}

void registerCommands() {
  static bool registered = false;
  if (registered)
    return;
  registered = true;

  // The old weaponres g_EventCmdSlot handler: TextMenu first (its own
  // chain subscription swallows), then the weapon selection walk.
  for (int i = 0; i < 10; i++) {
    if (abc::CChainedCommand *chained = commands::slotCommand(i)) {
      chained->Event().append([i]() {
        if (textmenuOpen())
          return true;
        weaponres::selectSlot(i, 1, false);
        return true;
      });
    }
  }
  if (abc::CChainedCommand *next = commands::get("invnext")) {
    next->Event().append([]() {
      weaponres::selectSlot(weaponres::selectedSlot(), 1, true);
      return true;
    });
  }
  if (abc::CChainedCommand *prev = commands::get("invprev")) {
    prev->Event().append([]() {
      weaponres::selectSlot(weaponres::selectedSlot(), -1, true);
      return true;
    });
  }
  if (abc::CChainedCommand *attack = commands::get("+attack")) {
    attack->Event().append([]() {
      // Old input pair: MOUSE_LEFT press confirmed the highlight
      // (Viewport g_EventHudMousePressed -> SelectWeapon) and that same
      // press was swallowed (m_bSelectBlock -> BlockAttackOnce). The
      // port keys both halves off +attack: visible bar -> confirm and
      // consume; otherwise the attack runs.
      return !weaponchoose::attackConfirm();
    });
  }
}

} // namespace weaponchoose

namespace {

constexpr float kSlotIcon = 15.0f;
constexpr float kItemActiveW = 60.0f;
constexpr float kItemActiveH = 20.0f;
constexpr float kItemInactiveW = 20.0f;
constexpr float kItemInactiveH = 20.0f;
constexpr float kGap = 1.0f;
constexpr float kCloseTime = 5.0f;

// Old RGBtoHSV/HSVtoRGB pair, hue-shifted - the rainbow tint cycle.
void rainbowShift(int counter, unsigned char &r, unsigned char &g,
                  unsigned char &b) {
  float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
  const float mx = std::max({rf, gf, bf});
  const float mn = std::min({rf, gf, bf});
  float h = 0.0f;
  const float v = mx;
  const float s = mx > 0.0f ? (mx - mn) / mx : 0.0f;
  if (mx > mn) {
    if (mx == rf)
      h = (gf - bf) / (mx - mn) + (gf < bf ? 6.0f : 0.0f);
    else if (mx == gf)
      h = (bf - rf) / (mx - mn) + 2.0f;
    else
      h = (rf - gf) / (mx - mn) + 4.0f;
    h *= 60.0f;
  }
  h = std::fmod(h + counter * 20.0f, 360.0f);
  const float c = v * s;
  const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
  const float m = v - c;
  float r2 = 0, g2 = 0, b2 = 0;
  if (h < 60) {
    r2 = c;
    g2 = x;
  } else if (h < 120) {
    r2 = x;
    g2 = c;
  } else if (h < 180) {
    g2 = c;
    b2 = x;
  } else if (h < 240) {
    g2 = x;
    b2 = c;
  } else if (h < 300) {
    r2 = x;
    b2 = c;
  } else {
    r2 = c;
    b2 = x;
  }
  r = static_cast<unsigned char>((r2 + m) * 255.0f);
  g = static_cast<unsigned char>((g2 + m) * 255.0f);
  b = static_cast<unsigned char>((b2 + m) * 255.0f);
}

} // namespace

WeaponChoosePanel::WeaponChoosePanel()
    : HudPanel("weaponchoose", "abc/abcui/hud/rml/weaponchoose.rml") {}

WeaponChoosePanel::~WeaponChoosePanel() = default;

void WeaponChoosePanel::reset() {
  Visible = false;
  hide();
}

void WeaponChoosePanel::setVisible(bool visible) {
  if (visible == Visible)
    return;
  Visible = visible;
  visible ? show() : hide();
}

bool WeaponChoosePanel::shouldDraw() const {
  // Old ShouldDraw gate chain.
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (!local)
    return false;
  if (playerres::isEntitySpectate(local->index))
    return false;
  if (hudstate::isHudHidden(hudstate::kHideAll | hudstate::kHideWeapons))
    return false;
  if (!hudstate::hasSuit())
    return false;
  const playerres::PlayerInfo *localInfo = playerres::getLocalPlayerInfo();
  if (!localInfo || localInfo->health <= 0)
    return false;
  // Old hud_fastswitch check: the weapon command already went out from
  // applySelection, the bar never shows.
  if (weaponres::fastSwitchOn())
    return false;
  return true;
}

void WeaponChoosePanel::rebuild() {
  Rml::Element *box = element("box");
  if (!box)
    return;

  // Owned weapons grouped by slot (old InsertWeapon slot lists; the
  // registry also carries the full WeaponList - ownership filters it).
  std::vector<const weaponres::Weapon *> bySlot[weaponres::kMaxWeaponSlot];
  for (const weaponres::Weapon *weapon : weaponres::allWeapons()) {
    if (!weapon || weapon->slot < 0 ||
        weapon->slot >= static_cast<int>(weaponres::kMaxWeaponSlot))
      continue;
    if (!weaponres::isOwned(weapon))
      continue;
    bySlot[weapon->slot].push_back(weapon);
  }
  for (auto &list : bySlot) {
    std::sort(list.begin(), list.end(),
              [](const weaponres::Weapon *a, const weaponres::Weapon *b) {
                return a->slotPos < b->slotPos;
              });
  }

  const weaponres::Weapon *selected = weaponres::selectedWeapon();
  const int activeSlot = weaponres::selectedSlot();

  std::string buffer;
  std::string rml;
  rml.reserve(4096);
  float x = 0.0f;
  float maxHeight = kSlotIcon;
  RainbowCounter = 0;
  // Highlighted item bounds, recorded in the loop below (old
  // m_pHandledWeapon GetBounds - the bucket rides them, y included).
  float selX = 0.0f, selY = 0.0f, selW = 0.0f, selH = 0.0f;
  bool selFound = false;

  // Old item bars: abw=w/10, abx=w/20, abg=w/30, abh=h/10; fill rides the
  // reserve count over max (scheme AmmoBar colors, empty = Red).
  auto appendAmmoBars = [&](const weaponres::Weapon *weapon, float w, float h) {
    const float abw = w / 10.0f;
    const float abx = w / 20.0f;
    const float abg = w / 30.0f;
    const float abh = h / 10.0f;
    const int ammoTypes[2] = {weapon->ammoType, weapon->ammo2Type};
    const int maxes[2] = {weapon->max1, weapon->max2};
    for (int bar = 0; bar < 2; bar++) {
      if (ammoTypes[bar] < 0)
        continue;
      const int count = weaponres::countAmmo(ammoTypes[bar]);
      const float ratio =
          maxes[bar] > 0
              ? std::clamp(static_cast<float>(count) / maxes[bar], 0.0f, 1.0f)
              : 1.0f;
      const float bx = abx + bar * (abw + abg);
      buffer = std::format(
          "<div class='bar-bg' style='left:{:.1f}dp; top:0dp; "
          "width:{:.1f}dp; height:{:.1f}dp;'></div>"
          "<div class='bar-fill' style='left:{:.1f}dp; top:0dp; "
          "width:{:.1f}dp; height:{:.1f}dp; "
          "background-color: {};'></div>",
          bx, abw, abh, bx, abw * ratio, abh,
          count > 0 ? "rgba(255, 255, 0, 125)" : "rgba(255, 0, 0, 255)");
      rml += buffer;
    }
  };

  for (size_t slot = 0; slot < weaponres::kMaxWeaponSlot; slot++) {
    float y = 0.0f;
    // Slot icon row (old Slot1..Slot9, Slot0 order with the matching
    // slotN_icon assets; the icons tint by scheme SlotColor). Additive
    // (texadd://) like the rest of the HUD ink, so the digits glow over the
    // scene instead of sitting on it.
    buffer = std::format("<div class='slot' style='left:{:.0f}dp; top:0dp;'>"
                         "<img src='texadd://abc/abcui/hud/img/weaponchoose/"
                         "slot{}_icon.png' "
                         "style='image-color: rgba(255, 176, 0, 160);'/></div>",
                         x, (slot + 1) % 10);
    rml += buffer;
    y = kSlotIcon + kGap;

    const bool expanded = static_cast<int>(slot) == activeSlot &&
                          selected != nullptr &&
                          selected->slot == static_cast<int>(slot);
    for (const weaponres::Weapon *weapon : bySlot[slot]) {
      const bool active = selected == weapon;
      const float w = expanded ? kItemActiveW : kItemInactiveW;
      const float h = expanded ? kItemActiveH : kItemInactiveH;

      // Old UpdateColor: ammo state picks the scheme tint, rainbow
      // rotates the hue per item (ActiveColor Default, InactiveColor
      // DefaultTransparent, EmptyColor Red, EmptyInactive 125 0 0 80).
      unsigned char r = 255, g = 176, b = 0, a = 255;
      unsigned char ri = 255, gi = 176, bi = 0, ai = 125;
      if (!weaponres::hasAmmo(weapon)) {
        r = 255, g = 0;
        ri = 125, gi = 0, ai = 80;
      }
      if (weaponchoose::RainbowMenu && weaponchoose::RainbowMenu->value > 0) {
        rainbowShift(RainbowCounter, r, g, b);
        rainbowShift(RainbowCounter, ri, gi, bi);
      }

      // Active sprite while highlighted, inactive otherwise; the ammo
      // bar pair rides the item top (old item PerformLayout).
      const weaponres::Weapon::SpriteSource &source =
          active ? weapon->active : weapon->inactive;
      std::string img;
      if (source.valid) {
        img = std::format(
            "<img src='spricon://{}/{},{},{},{}' style='width:100%; "
            "height:100%; image-color: rgba({}, {}, {}, {});'/>",
            source.atlas, source.rect.left, source.rect.top, source.rect.right,
            source.rect.bottom, active ? r : ri, active ? g : gi,
            active ? b : bi, active ? a : ai);
      }

      buffer =
          std::format("<div class='item{}' style='left:{:.0f}dp; top:{:.0f}dp; "
                      "width:{:.0f}dp; height:{:.0f}dp;'>",
                      active ? " active" : "", x, y, w, h);
      rml += buffer;
      if (active) {
        selX = x;
        selY = y;
        selW = w;
        selH = h;
        selFound = true;
      }
      appendAmmoBars(weapon, w, h);
      rml += img;
      rml += "</div>";
      y += h + kGap;
      RainbowCounter++;
    }
    maxHeight = std::max(maxHeight, y);
    x += (expanded ? kItemActiveW : kItemInactiveW) + kGap;
  }

  // Bucket frame over the highlighted item (old select_icon ImagePanel -
  // the bounds came straight off the item, y included). Missing from the
  // rebuilt list (dropped while shown) hides the frame, like the old
  // ChooseWeapon empty-slot path.
  if (selFound) {
    buffer = std::format(
        "<img id='bucket' src='../img/weaponchoose/select_icon.png' "
        "style='left:{:.0f}dp; top:{:.0f}dp; "
        "width:{:.0f}dp; height:{:.0f}dp; image-color: rgba(255, 255, 0, "
        "255);'/>",
        selX, selY, selW, selH);
    rml += buffer;
  }

  if (Rml::Element *content = element("content")) {
    content->SetInnerRML(rml);
    content->SetProperty("width", std::to_string(x) + "dp");
    content->SetProperty("height", std::to_string(maxHeight) + "dp");
  }
}

bool WeaponChoosePanel::confirmSelection() {
  // Old SelectWeapon: the highlighted pick must be usable (ammo or
  // SELECTONEMPTY); confirm sends the weapon command + select sound and
  // closes the bar. Rejected picks keep the bar open and let the attack
  // through (the old m_bSelectBlock armed only on the success path).
  const weaponres::Weapon *weapon = weaponres::selectedWeapon();
  if (!weapon || !Visible || !shouldDraw() || !weaponres::isOwned(weapon))
    return false;
  if (weaponres::fastSwitchOn())
    return false;
  if (!(weapon->flags & weaponres::kItemFlagSelectOnEmpty) &&
      !weaponres::hasAmmo(weapon))
    return false;
  gEngfuncs.pfnServerCmd(weapon->name.c_str());
  gEngfuncs.pfnPlaySoundByName("common/wpn_select.wav", 1.0f);
  setVisible(false);
  return true;
}

bool WeaponChoosePanel::attackConfirm() {
  if (!Visible)
    return false;
  return confirmSelection();
}

void WeaponChoosePanel::onDocumentReady() {
  subscribe(weaponres::ChooseWeapon, [this](const weaponres::Weapon *weapon) {
    // Old ChooseWeapon: hudon sound on the first open of a selection
    // round, moveselect on each highlight move; the bar closes after
    // close_time without input.
    if (!shouldDraw())
      return;
    const bool firstOpen = !Visible;
    if (!weapon || !weaponres::hasWeapon(weapon->slot, weapon->slotPos)) {
      // Highlight walked off an owned weapon: empty slot - close.
      setVisible(false);
      return;
    }
    if (firstOpen) {
      gEngfuncs.pfnPlaySoundByName("common/wpn_hudon.wav", 1.0f);
      setVisible(true);
      rebuild();
    } else {
      gEngfuncs.pfnPlaySoundByName("common/wpn_moveselect.wav", 1.0f);
      rebuild();
    }
    CloseAtTime = ClientTimeNow + kCloseTime;
  });
  subscribe(weaponres::DropAllWeapons, [this]() { setVisible(false); });
  subscribe(weaponres::DropWeapon, [this](const weaponres::Weapon *) {
    if (Visible)
      rebuild();
  });
  subscribe(weaponres::PickupWeapon, [this](const weaponres::Weapon *) {
    if (Visible)
      rebuild();
  });
  setVisible(false);
}

void WeaponChoosePanel::onData(double frameTime) {
  ClientTimeNow += frameTime;
  if (!Document)
    return;
  // Old OnThink: the bar closes itself after close_time (re-armed on every
  // ChooseWeapon through the rebuild path).
  if (Visible && CloseAtTime > 0.0 && ClientTimeNow >= CloseAtTime) {
    CloseAtTime = 0.0;
    setVisible(false);
  }
}

} // namespace panels
