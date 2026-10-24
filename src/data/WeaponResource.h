#pragma once

// data::weaponres - weapon registry, ammo counts, per-weapon sprite sources
// and the slot-selection machinery (batch C). Ported from ABCEnchance
// src/core/resource/weaponresource.* (value-semantic rebuild per plan 09 §5).
// The old SelectSlot pointer-list walk is rebuilt as sorted position lookups
// over the registry; its observable behavior (slot keys enter/cycle a slot,
// invnext/invprev walk slots and positions, fastswitch sends the weapon
// command directly, otherwise WeaponChoose highlights) is preserved.
//
// Consumers: AmmoBar/AmmoStack/WeaponStack (batch A sprites), Crosshair
// (batch B), WeaponChoose (batch C) via the Choose*/Pickup* events below.
// The ownership script events the old code fired here belong to AbcEssential
// signals (own diff) and are not duplicated.
//
// Sprite sources: sprites/<sprName>.txt parsed via SPR_GetList, atlas file +
// rect stored per slot; panels build spricon:// URIs through data::sprites.

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include <abc/Events.h>
#include <wrect.h>

struct weapon_data_s;

namespace weaponres {

constexpr size_t kMaxWeaponSlot = 10;
constexpr size_t kInvalidWeaponSlot = kMaxWeaponSlot + 1;

// Old CAmmoPanel::WEAPONSTATE - CurWeapon state bits the AmmoBar keys on.
enum WeaponState {
  kWeaponStateNotValid = 0,
  kWeaponStateValid = 1 << 0,
  kWeaponStateOnTarget = 1 << 1,
  kWeaponStateUzi = 1 << 2,
};

// Old ITEM_FLAGS (subset the registry logic uses).
enum ItemFlags {
  kItemFlagSelectOnEmpty = 1 << 0,
  kItemFlagExhaustible = 1 << 4,
};

struct Weapon {
  std::string name;
  std::string sprName;
  int ammoType = -1;
  int ammo2Type = -1;
  int max1 = -1;
  int max2 = -1;
  int slot = 0;
  int slotPos = 0;
  int flags = 0;
  int id = 0;
  int clip = 0;
  int clip2 = 0;
  int state = 0;

  // Sprite source (sprites/<atlas> crop). invalid = the entry was missing
  // from the weapon's sprite list.
  struct SpriteSource {
    std::string atlas;
    wrect_t rect{};
    bool valid = false;
  };
  SpriteSource active;    // "weapon_s"
  SpriteSource inactive;  // "weapon"
  SpriteSource ammo;      // "ammo"
  SpriteSource ammo2;     // "ammo2" (no implicit fallback)
  SpriteSource crosshair; // batch B
  SpriteSource autoaim;   // batch B
  SpriteSource zoomedCrosshair;
  SpriteSource zoomedAutoaim;
};

// HUD_Init-time: subscribes the netmsg weapon feed. Idempotent.
void init();
// Map transition (HUD_VidInit chain): registry starts from scratch.
void reset();
// Prediction sync (HUD_TxferPredictionData chain): ownership + clip diff.
void onTxferPredictionData(const weapon_data_s *weaponDataArray);

const Weapon *getWeapon(int id);
const Weapon *getWeapon(const char *name);
const Weapon *getWeapon(int slot, int pos);
// Every registered weapon (server WeaponList entries); the UI enumerates this
// instead of assuming the weapon id is a dense array index.
std::vector<const Weapon *> allWeapons();
bool hasWeapon(int slot, int pos);
// Ownership check for a registry weapon (old m_pOwnedWeaponData membership).
bool isOwned(const Weapon *weapon);
const Weapon *currentWeapon();

// Old hud_fastswitch read (SetSelectWeapon/ShouldDraw gate).
bool fastSwitchOn();

int countAmmo(int ammoType);
// Old HasAmmo: unlimited weapons (max1/max2 == -1) always true.
bool hasAmmo(const Weapon *weapon);
// Old GetAmmoPicFromWeapon: icon source of the ammo type, from any weapon
// using it. Returns false when no weapon carries the type.
bool ammoIcon(int ammoType, std::string &atlas, wrect_t &rect);

#pragma region slot selection (batch C, WeaponChoose data feed)
// The UI selection highlight (old m_pNowSelected) and its slot.
const Weapon *selectedWeapon();
int selectedSlot();

// Old SelectSlot: slot keys enter a slot (then cycle within it), the
// invnext/invprev wheel walks slots and positions. Only owned+usable weapons
// (ammo or SELECTONEMPTY) participate. With hud_fastswitch the weapon command
// is sent directly; otherwise ChooseWeapon fires so the UI can highlight.
// The TextMenu-open gate lives at the subscription site (panel assembly).
void selectSlot(int slot, int advance, bool wheel);

// Selection/ownership feeds for the WeaponChoose UI.
extern abc::CallbackList<void(const Weapon *)>
    ChooseWeapon; // highlight changed
// hud_fastswitch requested an immediate weapon change. The engine command is
// sent by the UI/module subscriber, never by this data registry.
extern abc::CallbackList<void(const Weapon *)> FastSwitchWeapon;
extern abc::CallbackList<void(const Weapon *)> PickupWeapon;
extern abc::CallbackList<void(const Weapon *)> DropWeapon;
extern abc::CallbackList<void()> DropAllWeapons;
#pragma endregion

} // namespace weaponres
