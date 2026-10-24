// data::weaponres implementation - see WeaponResource.h.
// from: ABCEnchance/src/core/resource/weaponresource.* (value-semantic
// rebuild; single store + id sets replace the three pointer lists).
#include <metahook.h>

#include "WeaponResource.h"
#include "../framework/ResourceLimits.h"

#include <cdll_int.h>
#include <wrect.h>
#include <weaponinfo.h>

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include <abc/Console.h>
#include <abc/CvarCmd.h>

#include <abcui/events.h>
#include <string_view>

#include "modsys/EventBus.h"

#include "HudState.h"
#include "NetworkMessages.h"
#include "framework/ScreenMetrics.h"

namespace weaponres {

abc::CallbackList<void(const Weapon *)> ChooseWeapon;
abc::CallbackList<void(const Weapon *)> FastSwitchWeapon;
abc::CallbackList<void(const Weapon *)> PickupWeapon;
abc::CallbackList<void(const Weapon *)> DropWeapon;
abc::CallbackList<void()> DropAllWeapons;

namespace {

// Module-facing mirror of the typed ownership/highlight feeds. The payload
// is the registry identity; a null weapon publishes slot -1 ("nothing").
void publishWeapon(const char *channel, const Weapon *weapon) {
  const AbcUiWeaponEvent event{weapon ? weapon->slot : -1,
                               weapon ? weapon->slotPos : -1};
  framework::eventBus().publish(channel, event);
}

constexpr int kMaxWeaponName = 128;
constexpr int kMaxAmmoTypes =
    256; // AmmoX byte domain; -1 is the no-ammo sentinel.
// Dense prediction-array capacity, not the weapon-id domain. Sven plugin
// weapons use ids beyond this; their ownership arrives through CurWeapon /
// WeapPickup instead of being read out of the fixed array.
constexpr int kPredictionWeaponSlots = 64;

struct SlotData {
  std::map<int, int> positions; // slotPos -> weapon id
  int minPos = 0;
  int maxPos = 0;

  void updateMinMax() {
    if (positions.empty()) {
      minPos = maxPos = 0;
      return;
    }
    minPos = positions.begin()->first;
    maxPos = positions.rbegin()->first;
  }
};

struct Registry {
  std::unordered_map<int, Weapon> byId;
  std::unordered_map<std::string, int> byName;
  SlotData slots[kMaxWeaponSlot];
  std::set<int> owned;
  std::set<int> available;
  std::unordered_map<int, int> ammo;
  const Weapon *curWeapon = nullptr;
  int curWeaponId = 0;
  // UI selection highlight (old m_pNowSelected / m_iNowSlot).
  const Weapon *selected = nullptr;
  int selectedSlotId = kInvalidWeaponSlot;

  void clear() {
    byId.clear();
    byName.clear();
    for (auto &slot : slots) {
      slot.positions.clear();
      slot.minPos = slot.maxPos = 0;
    }
    owned.clear();
    available.clear();
    ammo.clear();
    curWeapon = nullptr;
    curWeaponId = 0;
    selected = nullptr;
    selectedSlotId = kInvalidWeaponSlot;
  }
};

Registry RegistryState;
cvar_t *cvar_fastswitch = nullptr;

// Weapon usable for selection: owned and (ammo or SELECTONEMPTY).
bool selectable(const Weapon &weapon) {
  return RegistryState.owned.count(weapon.id) > 0 &&
         (hasAmmo(&weapon) || (weapon.flags & kItemFlagSelectOnEmpty) != 0);
}

// Owned+selectable weapon ids of one slot ordered by slotPos.
std::vector<const Weapon *> slotWeapons(int slot) {
  std::vector<const Weapon *> result;
  if (slot < 0 || slot >= static_cast<int>(kMaxWeaponSlot))
    return result;
  for (const auto &[pos, id] : RegistryState.slots[slot].positions) {
    const auto itor = RegistryState.byId.find(id);
    if (itor != RegistryState.byId.end() && selectable(itor->second))
      result.push_back(&itor->second);
  }
  return result;
}

std::vector<const Weapon *> allSelectableWeapons() {
  std::vector<const Weapon *> result;
  for (const auto &[id, weapon] : RegistryState.byId) {
    if (selectable(weapon))
      result.push_back(&weapon);
  }
  std::sort(result.begin(), result.end(), [](const Weapon *a, const Weapon *b) {
    if (a->slot != b->slot)
      return a->slot < b->slot;
    return a->slotPos < b->slotPos;
  });
  return result;
}

// Old SetSelectWeapon minus the TextMenu gate (lives at the subscription):
// fastswitch runs the weapon command right away, otherwise the UI highlight
// event fires and the panel drives the confirm.
void applySelection(const Weapon *weapon) {
  RegistryState.selected = weapon;
  RegistryState.selectedSlotId = weapon ? weapon->slot : kInvalidWeaponSlot;
  if (weapon && fastSwitchOn())
    FastSwitchWeapon(weapon);
  ChooseWeapon(weapon);
  publishWeapon(ABCUI_CHANNEL_HUD_CHOOSE_WEAPON, weapon);
}

// client_sprite_t* lookup by name + res (old GetSpriteList helper).
const client_sprite_t *findSpriteEntry(const client_sprite_t *list, int count,
                                       const char *name, int res) {
  if (!list)
    return nullptr;
  for (int i = 0; i < count; i++, list++) {
    if (std::string_view(name) == list->szName && list->iRes == res)
      return list;
  }
  return nullptr;
}

void fillSpriteSource(Weapon::SpriteSource &source, const char *atlasName,
                      const wrect_t &rect) {
  if (!atlasName || !framework::ResourceLimits::resourceName(atlasName))
    return;
  source.atlas = std::string(atlasName) + ".spr";
  source.rect = rect;
  source.valid = true;
}

// Record only actual server-provided sprite entries. Presentation fallbacks
// belong to consumers, not the registry.
void loadWeaponSprites(Weapon &weapon, const char *customSpr) {
  weapon.active = {};
  weapon.inactive = {};
  weapon.ammo = {};
  weapon.ammo2 = {};
  weapon.crosshair = {};
  weapon.autoaim = {};
  weapon.zoomedCrosshair = {};
  weapon.zoomedAutoaim = {};

  if (!framework::ResourceLimits::resourceName(weapon.sprName) ||
      (customSpr && *customSpr &&
       !framework::ResourceLimits::resourceName(customSpr)))
    return;
  std::string listPath = "sprites/";
  if (customSpr && *customSpr) {
    listPath += customSpr;
    listPath += '/';
  }
  listPath += weapon.sprName + ".txt";
  if (listPath.size() >= 256)
    return;

  int count = 0;
  const client_sprite_t *list =
      gEngfuncs.pfnSPR_GetList(listPath.data(), &count);
  if (!list)
    return;

  const int screenW = framework::ScreenWidth();
  const int res = screenW < 640 ? 320 : 640;

  const auto load = [&](const char *type, Weapon::SpriteSource &source) {
    if (const client_sprite_t *entry = findSpriteEntry(list, count, type, res))
      fillSpriteSource(source, entry->szSprite, entry->rc);
  };

  load("crosshair", weapon.crosshair);
  load("autoaim", weapon.autoaim);
  load("zoom", weapon.zoomedCrosshair);
  load("zoom_autoaim", weapon.zoomedAutoaim);
  load("weapon", weapon.inactive);
  load("weapon_s", weapon.active);
  load("ammo", weapon.ammo);
  load("ammo2", weapon.ammo2);
}

// Old WeaponList::Add - duplicate ids rejected, position collisions bump the
// slot position persistently (same as the old in-place mutation).
void addWeapon(Weapon &&incoming) {
  const int id = incoming.id;
  if (id <= 0 || incoming.slot < 0 ||
      incoming.slot >= static_cast<int>(kMaxWeaponSlot))
    return;
  if (RegistryState.byId.count(id))
    return;
  const bool wasOwned = RegistryState.owned.count(id) > 0;

  Weapon &weapon = RegistryState.byId[id] = std::move(incoming);
  RegistryState.byName[weapon.name] = id;

  SlotData &slot = RegistryState.slots[weapon.slot];
  while (slot.positions.count(weapon.slotPos))
    weapon.slotPos++;
  slot.positions[weapon.slotPos] = id;
  slot.updateMinMax();
  loadWeaponSprites(weapon, nullptr);
  if (wasOwned) {
    PickupWeapon(&weapon);
    publishWeapon(ABCUI_CHANNEL_HUD_PICKUP_WEAPON, &weapon);
  }
}

void dropAllWeapons() {
  RegistryState.owned.clear();
  RegistryState.available.clear();
  RegistryState.curWeapon = nullptr;
  RegistryState.curWeaponId = 0;
  RegistryState.selected = nullptr;
  RegistryState.selectedSlotId = kInvalidWeaponSlot;
  DropAllWeapons();
  framework::eventBus().publishRaw(ABCUI_CHANNEL_HUD_DROP_ALL_WEAPONS, nullptr);
}

// Ownership signals for ids outside the dense prediction array. WeapPickup
// and CurWeapon arrive for plugin weapons even when weaponDataArray cannot
// represent their id; keep the id in owned so a later WeaponList registration
// and the weapon menu see it.
void markOwned(int id) {
  if (id <= 0 || !RegistryState.owned.insert(id).second)
    return;
  const auto itor = RegistryState.byId.find(id);
  if (itor != RegistryState.byId.end()) {
    PickupWeapon(&itor->second);
    publishWeapon(ABCUI_CHANNEL_HUD_PICKUP_WEAPON, &itor->second);
  }
}

#pragma region netmsg handlers
void onAmmoX(int index, int count) {
  if (index < 0 || index >= kMaxAmmoTypes || count < 0)
    return;
  RegistryState.ammo[index] = count;
}

void onWeaponList(const netmsg::WeaponListData &data) {
  // The name is server-controlled and is later sent verbatim to the engine
  // console by WeaponChoosePanel::confirmSelection; it is also the sprite
  // base name. Accept only a command-safe identifier (AbcEssential applies
  // the same rule to its signal feed).
  if (data.id <= 0 || data.slot < 0 ||
      data.slot >= static_cast<int>(kMaxWeaponSlot) || data.slotPos < 0 ||
      !framework::ResourceLimits::identifierName(data.name))
    return;
  if (const auto itor = RegistryState.byId.find(data.id);
      itor != RegistryState.byId.end()) {
    // Old UpdateWeapon: full replace + sprite reload, ownership kept.
    Weapon updated;
    updated.name = data.name;
    updated.sprName = data.name;
    updated.ammoType = data.ammoType;
    updated.max1 = data.max1;
    updated.ammo2Type = data.ammo2Type;
    updated.max2 = data.max2;
    updated.slot = data.slot;
    updated.slotPos = data.slotPos;
    updated.id = data.id;
    updated.flags = data.flags;
    Weapon &weapon = itor->second;
    updated.clip = weapon.clip;
    updated.clip2 = weapon.clip2;
    updated.state = weapon.state;
    auto oldName = RegistryState.byName.find(weapon.name);
    if (oldName != RegistryState.byName.end() && oldName->second == weapon.id)
      RegistryState.byName.erase(oldName);
    auto &oldSlot = RegistryState.slots[weapon.slot];
    oldSlot.positions.erase(weapon.slotPos);
    oldSlot.updateMinMax();
    weapon = std::move(updated);
    RegistryState.byName[weapon.name] = weapon.id;
    auto &newSlot = RegistryState.slots[weapon.slot];
    while (newSlot.positions.count(weapon.slotPos))
      ++weapon.slotPos;
    newSlot.positions[weapon.slotPos] = weapon.id;
    newSlot.updateMinMax();
    if (RegistryState.selected == &weapon)
      RegistryState.selectedSlotId = weapon.slot;
    loadWeaponSprites(weapon, nullptr);
    return;
  }

  Weapon created;
  created.name = data.name;
  created.sprName = data.name;
  created.ammoType = data.ammoType;
  created.max1 = data.max1;
  created.ammo2Type = data.ammo2Type;
  created.max2 = data.max2;
  created.slot = data.slot;
  created.slotPos = data.slotPos;
  created.id = data.id;
  created.flags = data.flags;
  created.clip = 0;
  addWeapon(std::move(created));
}

void onCustWeapon(int id, const char *sprName) {
  const auto itor = RegistryState.byId.find(id);
  if (itor == RegistryState.byId.end())
    return;
  loadWeaponSprites(itor->second, sprName);
}

void onCurWeapon(int state, int id, int clip, int clip2) {
  if (state > 0) {
    if (id == -1) {
      dropAllWeapons();
      return;
    }
    markOwned(id);
    RegistryState.curWeaponId = id;
    RegistryState.curWeapon = getWeapon(id);
    if (const auto itor = RegistryState.byId.find(id);
        itor != RegistryState.byId.end()) {
      itor->second.clip = clip;
      itor->second.clip2 = clip2;
      itor->second.state = state;
    }
    return;
  }
  if (id == -1 || id == 0)
    dropAllWeapons();
}

void onWeapPickup(int id) {
  markOwned(id);
}

void onWeaponSpr(int id, const char *sprName) {
  if (!sprName || !framework::ResourceLimits::resourceName(sprName))
    return;
  const auto itor = RegistryState.byId.find(id);
  if (itor == RegistryState.byId.end() || id <= 0)
    return;
  itor->second.sprName = sprName;
  loadWeaponSprites(itor->second, nullptr);
}
#pragma endregion

} // namespace

void init() {
  static bool registered = false;
  if (registered)
    return;
  registered = true;
  cvar_fastswitch = abc::CVAR_GET_POINTER("hud_fastswitch");
  netmsg::AmmoX.append(onAmmoX);
  netmsg::WeaponList.append(onWeaponList);
  netmsg::CustWeapon.append(onCustWeapon);
  netmsg::CurWeapon.append(onCurWeapon);
  netmsg::WeaponSpr.append(onWeaponSpr);
  netmsg::WeapPickup.append(onWeapPickup);
}

void reset() {
  RegistryState.clear();
}

void onTxferPredictionData(const weapon_data_s *weaponDataArray) {
  if (!weaponDataArray)
    return;
  // Ownership diff against the prediction array. Only m_iId/m_iClip are
  // consumed: the old sync also copied a "m_iClip2" that lived past the
  // real weapon_data_t layout (fabricated WeaponData struct) - not ported.
  std::vector<const Weapon *> pickedUp;
  std::vector<const Weapon *> dropped;
  for (const auto &[id, weapon] : RegistryState.byId) {
    // The prediction array is dense and has no id table. Plugin weapon ids
    // above its capacity are owned through CurWeapon/WeapPickup below; do
    // not index past the array for them.
    if (id >= kPredictionWeaponSlots)
      continue;
    const weapon_data_s &data = weaponDataArray[id];
    if (data.m_iId <= 0) {
      if (RegistryState.owned.erase(id))
        dropped.push_back(&weapon);
    } else {
      if (RegistryState.owned.insert(id).second)
        pickedUp.push_back(&weapon);
      // Registry mutation: id keys are stable, only scalar fields move.
      RegistryState.byId[id].clip = data.m_iClip;
    }
  }
  for (const Weapon *weapon : pickedUp) {
    PickupWeapon(weapon);
    publishWeapon(ABCUI_CHANNEL_HUD_PICKUP_WEAPON, weapon);
  }
  for (const Weapon *weapon : dropped) {
    DropWeapon(weapon);
    publishWeapon(ABCUI_CHANNEL_HUD_DROP_WEAPON, weapon);
  }
  if (!dropped.empty() && RegistryState.owned.empty()) {
    dropAllWeapons();
    return;
  }
  // Current weapon pointer survives only while the weapon is still known.
  if (RegistryState.curWeaponId &&
      !RegistryState.byId.count(RegistryState.curWeaponId))
    RegistryState.curWeapon = nullptr;
  else if (RegistryState.curWeaponId)
    RegistryState.curWeapon = &RegistryState.byId[RegistryState.curWeaponId];
  if (RegistryState.selected &&
      !RegistryState.owned.count(RegistryState.selected->id)) {
    RegistryState.selected = nullptr;
    RegistryState.selectedSlotId = kInvalidWeaponSlot;
  }
}

const Weapon *getWeapon(int id) {
  const auto itor = RegistryState.byId.find(id);
  return itor != RegistryState.byId.end() ? &itor->second : nullptr;
}

const Weapon *getWeapon(const char *name) {
  const auto itor = RegistryState.byName.find(name);
  return itor != RegistryState.byName.end() ? getWeapon(itor->second) : nullptr;
}

const Weapon *getWeapon(int slot, int pos) {
  if (slot < 0 || slot >= static_cast<int>(kMaxWeaponSlot))
    return nullptr;
  const auto &positions = RegistryState.slots[slot].positions;
  const auto itor = positions.find(pos);
  return itor != positions.end() ? getWeapon(itor->second) : nullptr;
}

std::vector<const Weapon *> allWeapons() {
  std::vector<const Weapon *> result;
  result.reserve(RegistryState.byId.size());
  for (const auto &entry : RegistryState.byId)
    result.push_back(&entry.second);
  return result;
}

bool hasWeapon(int slot, int pos) {
  return getWeapon(slot, pos) &&
         RegistryState.owned.count(getWeapon(slot, pos)->id) > 0;
}

bool isOwned(const Weapon *weapon) {
  return weapon && RegistryState.owned.count(weapon->id) > 0;
}

bool fastSwitchOn() {
  return cvar_fastswitch && cvar_fastswitch->value > 0;
}

const Weapon *currentWeapon() {
  return RegistryState.curWeapon;
}

int countAmmo(int ammoType) {
  if (ammoType < 0 || ammoType >= kMaxAmmoTypes)
    return 0;
  const auto itor = RegistryState.ammo.find(ammoType);
  return itor != RegistryState.ammo.end() ? itor->second : 0;
}

bool hasAmmo(const Weapon *weapon) {
  if (!weapon)
    return false;
  // Weapons that need no ammo.
  if (weapon->max1 == -1 && weapon->max2 == -1)
    return true;
  if (weapon->ammoType > -1) {
    const bool has = weapon->clip > 0 || countAmmo(weapon->ammoType) > 0;
    if (weapon->ammo2Type > -1 && !has)
      return weapon->clip2 > 0 || countAmmo(weapon->ammo2Type) > 0;
    return has;
  }
  return true;
}

bool ammoIcon(int ammoType, std::string &atlas, wrect_t &rect) {
  for (const auto &[id, weapon] : RegistryState.byId) {
    if (weapon.ammoType == ammoType && weapon.ammo.valid) {
      atlas = weapon.ammo.atlas;
      rect = weapon.ammo.rect;
      return true;
    }
    if (weapon.ammo2Type == ammoType && weapon.ammo2.valid) {
      atlas = weapon.ammo2.atlas;
      rect = weapon.ammo2.rect;
      return true;
    }
  }
  return false;
}

const Weapon *selectedWeapon() {
  return RegistryState.selected;
}

int selectedSlot() {
  return RegistryState.selectedSlotId;
}

void selectSlot(int slot, int advance, bool wheel) {
  // Old SelectSlot entry gates: TextMenu-first (subscription site) and the
  // HEV suit (no suit, no weapons to walk).
  if (!hudstate::hasSuit())
    return;
  const std::vector<const Weapon *> pool = allSelectableWeapons();
  if (pool.empty())
    return;
  slot = std::clamp(slot, 0, static_cast<int>(kMaxWeaponSlot) - 1);

  // Wheel from a cold start selects the first usable weapon.
  if (wheel && !RegistryState.selected) {
    applySelection(pool.front());
    return;
  }

  std::vector<const Weapon *> inSlot = slotWeapons(slot);
  if (!wheel && slot != RegistryState.selectedSlotId) {
    // Entering a different slot: first position (advance<0 walks in from
    // the end - the old GetLastPos semantics).
    if (!inSlot.empty()) {
      applySelection(advance > 0 ? inSlot.front() : inSlot.back());
    }
    return;
  }

  // Same slot (or wheel mid-slot): step to the neighbour position. At the
  // slot edge the wheel rolls into the next/previous non-empty slot (old
  // GetMaxPos/GetMinPos check before the step); the slot key wraps within
  // the slot.
  const Weapon *current = RegistryState.selected;
  if (!current)
    return;

  if (wheel) {
    const std::vector<const Weapon *> &ownSlot = slotWeapons(current->slot);
    const bool atEdge = advance > 0
                            ? !ownSlot.empty() && ownSlot.back() == current
                            : !ownSlot.empty() && ownSlot.front() == current;
    if (atEdge) {
      // Old do-while: circular next/prev non-empty slot, entering at
      // its first (advance>0) / last position.
      std::vector<int> nonEmpty;
      for (int s = 0; s < static_cast<int>(kMaxWeaponSlot); s++) {
        if (!slotWeapons(s).empty())
          nonEmpty.push_back(s);
      }
      const int at = static_cast<int>(
          std::find(nonEmpty.begin(), nonEmpty.end(), current->slot) -
          nonEmpty.begin());
      const int base = at >= 0 ? at : 0;
      const int step = advance > 0 ? 1 : -1;
      const int next =
          nonEmpty[(base + step + static_cast<int>(nonEmpty.size())) %
                   nonEmpty.size()];
      const std::vector<const Weapon *> target = slotWeapons(next);
      applySelection(advance > 0 ? target.front() : target.back());
      return;
    }
  }

  if (!inSlot.empty() && (!wheel || current->slot == slot)) {
    for (size_t i = 0; i < inSlot.size(); i++) {
      if (inSlot[i] != current)
        continue;
      const int step = advance > 0 ? 1 : -1;
      const size_t next = (i + step + inSlot.size()) % inSlot.size();
      applySelection(inSlot[next]);
      return;
    }
  }
}

} // namespace weaponres
