// panels::WeaponStackPanel implementation - see WeaponStackPanel.h.
// from: ABCEnchance/src/hud/vgui/weaponstack.* (GBK->UTF-8).

#include "WeaponStackPanel.h"

#include <RmlUi/Core/Element.h>

#include <cstdio>

#include "../../data/NetworkMessages.h"
#include "../../data/WeaponResource.h"

namespace panels {

namespace {
constexpr const char *kWeaponIdAttribute = "weaponid";
} // namespace

WeaponStackPanel::WeaponStackPanel()
    : HudPanel("weaponstack", "abc/abcui/hud/rml/weaponstack.rml") {
  // Old WeaponStackPanel.res.
  StackParams.keepTime = 5.0f;
  StackParams.fadeIn = 0.1f;
  StackParams.fadeOut = 0.3f;
  StackParams.maxItems = 280 / 30; // tall 280 / item_tall 30
}

void WeaponStackPanel::onDocumentReady() {
  show();
  initStack(Document, "stack");

  subscribe(netmsg::WeapPickup, [this](int index) {
    const weaponres::Weapon *weapon = weaponres::getWeapon(index);
    if (!weapon || !weapon->inactive.valid)
      return;
    Rml::Element *item = appendItem("item", "<img class=\"icon\"/>");
    if (!item)
      return;
    // The tint/icon refresh keys off the weapon id attribute (old
    // CWeaponStackItem::m_iWeapon).
    item->SetAttribute(kWeaponIdAttribute, index);
  });
}

void WeaponStackPanel::onData(double frameTime) {
  tickItems(frameTime);

  // Old CWeaponStackItem::CheckExpire/ForceReload per tick: icon follows a
  // sprite reload (attribute src rebuilt from live data) and the empty
  // tint follows weaponres::hasAmmo.
  Rml::Element *container = stackContainer();
  if (!container)
    return;
  for (int i = 0; i < container->GetNumChildren(); i++) {
    Rml::Element *item = container->GetChild(i);
    const int weaponId = item->GetAttribute<int>(kWeaponIdAttribute, 0);
    const weaponres::Weapon *weapon = weaponres::getWeapon(weaponId);
    Rml::Element *icon = item->GetFirstChild();
    if (!weapon || !icon)
      continue;
    if (weapon->inactive.valid) {
      const wrect_t &rc = weapon->inactive.rect;
      const Rml::String src =
          "spricon://" + weapon->inactive.atlas + "/" +
          std::to_string(rc.left) + "," + std::to_string(rc.top) + "," +
          std::to_string(rc.right) + "," + std::to_string(rc.bottom);
      if (icon->GetAttribute<Rml::String>("src", "") != src)
        icon->SetAttribute("src", src);
    }
    const bool empty = !weaponres::hasAmmo(weapon);
    item->SetClass("empty", empty);
  }
}

void WeaponStackPanel::reset() {
  clearItems();
}

} // namespace panels
