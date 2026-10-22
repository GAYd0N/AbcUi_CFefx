// framework::WeaponsApi - the sdk weapons section (registry snapshots and the
// selection action). See ApiTable.h for how it joins the module table.
#include "ApiTable.h"

#include <cstring>
#include <string>

#include "../data/WeaponResource.h"
#include "../framework/ResourceLimits.h"

namespace framework {
namespace WeaponsApi {
namespace {

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
  out.valid = source.valid ? 1 : 0;
  out.atlas[0] = '\0';
  if (source.valid)
    copyField(source.atlas, out.atlas, sizeof(out.atlas));
  out.rect[0] = source.rect.left;
  out.rect[1] = source.rect.top;
  out.rect[2] = source.rect.right;
  out.rect[3] = source.rect.bottom;
}

// Copies `full` into the caller's buffer honoring the size it declared
// (ABI rule: a caller may be built against an older, shorter header; 0 means
// "the framework's current size").
void copyOut(const AbcUiWeaponInfo &full, AbcUiWeaponInfo &out) {
  const size_t declared = out.structSize;
  const size_t bytes =
      declared == 0 || declared > sizeof(full) ? sizeof(full) : declared;
  std::memcpy(&out, &full, bytes);
}

void fill(const weaponres::Weapon &weapon, AbcUiWeaponInfo &out) {
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
  copyOut(full, out);
}

int32_t apiList(AbcUiWeaponInfo *out, int32_t capacity) {
  const std::vector<const weaponres::Weapon *> weapons =
      weaponres::allWeapons();
  const int32_t total = static_cast<int32_t>(weapons.size());
  if (!out || capacity <= 0)
    return total;
  const int32_t count = total < capacity ? total : capacity;
  for (int32_t i = 0; i < count; ++i) {
    out[i] = AbcUiWeaponInfo{};
    fill(*weapons[static_cast<size_t>(i)], out[i]);
  }
  return total;
}

int32_t apiGet(int32_t slot, int32_t slotPos, AbcUiWeaponInfo *out) {
  if (!out)
    return 0;
  const weaponres::Weapon *weapon = weaponres::getWeapon(slot, slotPos);
  if (!weapon)
    return 0;
  *out = AbcUiWeaponInfo{};
  fill(*weapon, *out);
  return 1;
}

int32_t apiGetById(int32_t id, AbcUiWeaponInfo *out) {
  if (!out)
    return 0;
  const weaponres::Weapon *weapon = weaponres::getWeapon(id);
  if (!weapon)
    return 0;
  *out = AbcUiWeaponInfo{};
  fill(*weapon, *out);
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
  *out = AbcUiWeaponInfo{};
  fill(*weapon, *out);
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
  *out = AbcUiWeaponInfo{};
  fill(*weapon, *out);
  return 1;
}

int32_t apiAmmoIcon(int32_t ammoType, AbcUiWeaponSprite *out) {
  if (!out)
    return 0;
  std::string atlas;
  wrect_t rect{};
  if (!weaponres::ammoIcon(ammoType, atlas, rect))
    return 0;
  *out = AbcUiWeaponSprite{};
  out->valid = 1;
  copyField(atlas, out->atlas, sizeof(out->atlas));
  out->rect[0] = rect.left;
  out->rect[1] = rect.top;
  out->rect[2] = rect.right;
  out->rect[3] = rect.bottom;
  return 1;
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
