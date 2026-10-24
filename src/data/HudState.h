#pragma once
#include <string>

// data::hudstate - local player HUD state the visibility rules hang off:
// suit possession (weapon bits bit 31), hide-HUD tokens, longjump module
// presence and spectate mode. Replaces the scattered state the old Viewport
// kept (m_bitsHideHUDDisplay, m_bitsWeaponBits, the lj/iuser1 checks inside
// its HUD_UpdateClientData handler).
//
// Fed by the hook spine: onUpdateClientData from the HUD_UpdateClientData
// chain, onHideToken from the netmsg HideWeapon/HideHUD events (subscribed
// in init(), HUD_Init-time).

#include <abc/Events.h>

struct client_data_s;
struct clientdata_s;

namespace hudstate {

// Hide-HUD token bits (old Viewport.h HUD_HIDEWEAPONS...; the values arrive
// on the HideWeapon/HideHUD messages).
enum HudHideBits {
  kHideWeapons = 1 << 0,
  kHideFlashlight = 1 << 1,
  kHideAll = 1 << 2,
  kHideHealth = 1 << 3,
  kHideSelection = 1 << 4,
  kHideBattery = 1 << 5,
  kHideCustom1 = 1 << 6,
  kHideCustom2 = 1 << 7,
};

extern abc::CallbackList<void(bool)> SuitChanged;
extern abc::CallbackList<void(bool)> LongJumpChanged;
extern abc::CallbackList<void(bool)> SpectateChanged;
// Fires with the raw token whenever HideWeapon/HideHUD arrives (the old
// hudhideCallback ran for both messages).
extern abc::CallbackList<void(int)> HideTokenChanged;

void init();
void reset();
struct MessageSnapshot {
  int Health = 0;
  int Armor = 0;
  int FlashBattery = 100;
  bool FlashOn = false;
  std::string ServerName;
  std::string NextMap;
  int TimeEnd = 0;
};
const MessageSnapshot &messages();

// Spine feed (HUD_UpdateClientData chain). Tracks suit bits, longjump
// (physinfo "slj") and spectate (local player iuser1), firing the change
// events above.
void onUpdateClientData(const client_data_s *cdata);

// Spine feed (HUD_TxferLocalOverrides chain): captures the prediction client
// data pointer the old code read as gClientData (crosshair health/punchangle,
// Effect waterlevel). The pointer is engine-owned and refreshed every frame.
void onTxferLocalOverrides(const clientdata_s *client);

bool hasSuit();
bool hasLongJump();
bool isSpectate();
// Local player observer mode and target (entity_state iuser1/iuser2).
// 0 when not spectating; the module-facing snapshot exposes the same pair.
int spectateMode();
int spectateTarget();
// Raw hide tokens, last write wins (old Viewport parity): the most recent
// HideWeapon/HideHUD message's token replaces the previous one - bits do
// not accumulate across messages.
bool isHudHidden(int token);
// Probe aid: the raw last-write token value.
int debugHideTokens();

// Old gClientData readers (null until the first prediction frame).
float health();
int waterlevel();
const float *punchangle();
// Old s_flFov: the HUD_UpdateClientData fov (view zoom state).
float fov();

} // namespace hudstate
