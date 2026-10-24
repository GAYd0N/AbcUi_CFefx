// framework::WeaponsApi - the sdk weapons section (registry snapshots and the
// selection action). See ApiTable.h for how it joins the module table.
#include "ApiTable.h"
#include "ApiSnapshot.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "../data/WeaponResource.h"
#include "rmlui/texture/SpriteResource.h"
#include "../framework/ResourceLimits.h"

namespace framework {
namespace WeaponsApi {
namespace {

// These records are embedded or returned by the legacy typed-array API.

// Copies a string into a fixed field, always terminating.
void copyField(const std::string &text, char *out, size_t outSize) {
  if (!out || outSize == 0)
    return;
  size_t i = 0;
  for (; i + 1 < outSize && i < text.size(); ++i)
    out[i] = text[i];
  out[i] = '\0';
}

void copySprite(const weaponres::Weapon::SpriteSource &source,
                AbcUiWeaponSprite &out) {
  out.status = source.valid ? sprites::resourceStatus(source.atlas, source.rect)
                            : ABCUI_ERROR_NOT_FOUND;
  out.atlas[0] = '\0';
  if (out.status == ABCUI_OK)
    copyField(source.atlas, out.atlas, sizeof(out.atlas));
  out.rect[0] = source.rect.left;
  out.rect[1] = source.rect.top;
  out.rect[2] = source.rect.right;
  out.rect[3] = source.rect.bottom;
}

void fill(const weaponres::Weapon &weapon, void *out) {
  AbcUiWeaponInfo full{};
  full.structSize = sizeof(AbcUiWeaponInfo);
  full.slot = weapon.slot;
  full.slotPos = weapon.slotPos;
  full.ammoType = weapon.ammoType;
  full.ammo2Type = weapon.ammo2Type;
  full.max1 = weapon.max1;
  full.max2 = weapon.max2;
  full.flags = weapon.flags;
  full.owned = weaponres::isOwned(&weapon) ? 1 : 0;
  full.hasAmmo = weaponres::hasAmmo(&weapon) ? 1 : 0;
  full.clip = weapon.clip;
  full.clip2 = weapon.clip2;
  full.state = weapon.state;
  copyField(weapon.name, full.name, sizeof(full.name));
  copySprite(weapon.active, full.active);
  copySprite(weapon.inactive, full.inactive);
  copySprite(weapon.ammo, full.ammo);
  copySprite(weapon.ammo2, full.ammo2);
  copySprite(weapon.crosshair, full.crosshair);
  copySprite(weapon.autoaim, full.autoaim);
  copySprite(weapon.zoomedCrosshair, full.zoomedCrosshair);
  copySprite(weapon.zoomedAutoaim, full.zoomedAutoaim);
  abi::copySnapshot(out, full);
}

int32_t apiList(void *out, uint32_t capacity, uint32_t stride) {
  if (out && capacity != 0 && !abi::validArrayLayout(capacity, stride))
    return -1;
  const auto weapons = weaponres::allWeapons();
  const int32_t total = static_cast<int32_t>(weapons.size());
  if (!out || capacity == 0)
    return total;
  const uint32_t count = std::min(capacity, static_cast<uint32_t>(total));
  if (!abi::validSnapshotArray(out, count, stride))
    return -1;
  for (uint32_t i = 0; i < count; ++i)
    fill(*weapons[i], abi::arrayElement(out, i, stride));
  return total;
}

int32_t apiGet(int32_t slot, int32_t slotPos, AbcUiWeaponInfo *out) {
  if (!out)
    return 0;
  const weaponres::Weapon *weapon = weaponres::getWeapon(slot, slotPos);
  if (!weapon)
    return 0;
  fill(*weapon, out);
  return 1;
}

int32_t apiGetById(int32_t id, AbcUiWeaponInfo *out) {
  if (!out)
    return 0;
  const weaponres::Weapon *weapon = weaponres::getWeapon(id);
  if (!weapon)
    return 0;
  fill(*weapon, out);
  return 1;
}

int32_t apiSelectedSlot() {
  return weaponres::selectedSlot();
}

int32_t apiSelected(AbcUiWeaponInfo *out) {
  if (!out)
    return 0;
  const weaponres::Weapon *weapon = weaponres::selectedWeapon();
  if (!weapon)
    return 0;
  fill(*weapon, out);
  return 1;
}

int32_t apiCountAmmo(int32_t ammoType) {
  return weaponres::countAmmo(ammoType);
}

int32_t apiFastSwitch() {
  return weaponres::fastSwitchOn() ? 1 : 0;
}

void apiSelectSlot(int32_t slot, int32_t advance, int32_t wheel) {
  weaponres::selectSlot(slot, advance, wheel != 0);
}

int32_t apiValidCommandName(const char *name) {
  return name && ResourceLimits::weaponCommandName(name) ? 1 : 0;
}

int32_t apiCurrent(AbcUiWeaponInfo *out) {
  if (!out)
    return 0;
  const weaponres::Weapon *weapon = weaponres::currentWeapon();
  if (!weapon)
    return 0;
  fill(*weapon, out);
  return 1;
}

int32_t apiAmmoIcon(int32_t ammoType, AbcUiWeaponSprite *out) {
  if (!out)
    return ABCUI_ERROR_INVALID;
  std::string atlas;
  wrect_t rect{};
  if (!weaponres::ammoIcon(ammoType, atlas, rect)) {
    *out = {};
    out->status = ABCUI_ERROR_NOT_FOUND;
    return ABCUI_ERROR_NOT_FOUND;
  }
  *out = AbcUiWeaponSprite{};
  out->status = sprites::resourceStatus(atlas, rect);
  copyField(atlas, out->atlas, sizeof(out->atlas));
  out->rect[0] = rect.left;
  out->rect[1] = rect.top;
  out->rect[2] = rect.right;
  out->rect[3] = rect.bottom;
  return out->status;
}

// One initializer per member, in declaration order (see the console table
// note: a missing entry becomes a null pointer, not a compile error).
const AbcUiWeaponsApi kWeaponsApi = {
    sizeof(AbcUiWeaponsApi),
    apiList,
    apiGet,
    apiSelectedSlot,
    apiSelected,
    apiCountAmmo,
    apiFastSwitch,
    apiSelectSlot,
    apiValidCommandName,
    apiCurrent,
    apiGetById,
    apiAmmoIcon,
};

} // namespace

const AbcUiWeaponsApi *get() {
  return &kWeaponsApi;
}

} // namespace WeaponsApi
} // namespace framework
