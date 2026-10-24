// data::hudstate implementation - see HudState.h.
#include <metahook.h>

#include "HudState.h"

#include <cdll_int.h>
#include <entity_state.h>

#include <abc/Parse.h>
#include <abcui/events.h>

#include "../modsys/EventBus.h"
#include "NetworkMessages.h"

namespace hudstate {

namespace {

// Old Viewport: WEAPON_SUIT = 31.
constexpr int kWeaponSuitBit = 1 << 31;

bool Suit = false;
bool LongJump = false;
bool Spectate = false;
int SpectateMode = 0;
int SpectateTarget = 0;
int HideTokens = 0;
clientdata_s ClientDataValue{};
const clientdata_s *ClientData = nullptr;
float Fov = 90.0f;
MessageSnapshot Snapshot;

void onHideToken(int token) {
  // Old Viewport parity (Viewport.cpp hudhideCallback): plain assignment,
  // the newest message wins - see the isHudHidden contract in the header.
  HideTokens = token;
  HideTokenChanged(token);
  const AbcUiIntEvent moduleEvent{token};
  framework::eventBus().publish(ABCUI_CHANNEL_HUD_HIDE_TOKEN, moduleEvent);
}

} // namespace

abc::CallbackList<void(bool)> SuitChanged;
abc::CallbackList<void(bool)> LongJumpChanged;
abc::CallbackList<void(bool)> SpectateChanged;
abc::CallbackList<void(int)> HideTokenChanged;

void init() {
  static bool registered = false;
  if (registered)
    return;
  registered = true;
  SuitChanged = {};
  LongJumpChanged = {};
  SpectateChanged = {};
  HideTokenChanged = {};
  // The old hudhideCallback subscribed both messages with one handler.
  netmsg::Health.append([](int v) { Snapshot.Health = v; });
  netmsg::Battery.append([](int v) { Snapshot.Armor = v; });
  netmsg::FlashBat.append([](int v) { Snapshot.FlashBattery = v; });
  netmsg::Flashlight.append([](bool on, int v) {
    Snapshot.FlashOn = on;
    Snapshot.FlashBattery = v;
  });
  netmsg::ServerName.append(
      [](const char *v) { Snapshot.ServerName = v ? v : ""; });
  netmsg::NextMap.append([](const char *v) { Snapshot.NextMap = v ? v : ""; });
  netmsg::TimeEnd.append([](int v) { Snapshot.TimeEnd = v; });
  netmsg::HideWeapon.append(onHideToken);
  netmsg::HideHud.append(onHideToken);
}

void onUpdateClientData(const client_data_s *cdata) {
  if (!cdata)
    return;

  // Old exportfuncs HUD_UpdateClientData: s_flFov = c->fov (the zoom state
  // the crosshair autohide gate keys on).
  Fov = cdata->fov;

  const bool suit = (cdata->iWeaponBits & kWeaponSuitBit) != 0;
  if (suit != Suit) {
    Suit = suit;
    SuitChanged(suit);
    const AbcUiBoolEvent moduleEvent{suit ? 1 : 0};
    framework::eventBus().publish(ABCUI_CHANNEL_HUD_SUIT, moduleEvent);
  }

  // Longjump module: physinfo "slj", polled on the same cadence as the old
  // g_EventHudUpdateClientData handler.
  const bool longJump =
      abc::ParseInt(gEngfuncs.PhysInfo_ValueForKey("slj")).value_or(0) != 0;
  if (longJump != LongJump) {
    LongJump = longJump;
    LongJumpChanged(longJump);
    const AbcUiBoolEvent moduleEvent{longJump ? 1 : 0};
    framework::eventBus().publish(ABCUI_CHANNEL_HUD_LONG_JUMP, moduleEvent);
  }

  // Spectate: local player iuser1 (old SetSpectate check). The mode/target
  // pair rides the same call: iuser1 is the observer mode, iuser2 the target
  // entindex (both 0 when not spectating). Native SpectatorPanel reads the
  // same two fields through g_iUser1/g_iUser2.
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  SpectateMode = local ? local->curstate.iuser1 : 0;
  SpectateTarget = local ? local->curstate.iuser2 : 0;
  const bool spectate = SpectateMode > 0;
  if (spectate != Spectate) {
    Spectate = spectate;
    SpectateChanged(spectate);
    const AbcUiBoolEvent moduleEvent{spectate ? 1 : 0};
    framework::eventBus().publish(ABCUI_CHANNEL_HUD_SPECTATE, moduleEvent);
  }
}

const MessageSnapshot &messages() {
  return Snapshot;
}

void reset() {
  Snapshot = {};
  Suit = LongJump = Spectate = false;
  SpectateMode = SpectateTarget = 0;
  HideTokens = 0;
  ClientData = nullptr;
  ClientDataValue = {};
  Fov = 90;
}

bool hasSuit() {
  return Suit;
}

bool hasLongJump() {
  return LongJump;
}

bool isSpectate() {
  return Spectate;
}

int spectateMode() {
  return SpectateMode;
}

int spectateTarget() {
  return SpectateTarget;
}

bool isHudHidden(int token) {
  return (HideTokens & token) != 0;
}

int debugHideTokens() {
  return HideTokens;
}

void onTxferLocalOverrides(const clientdata_s *client) {
  // Old exportfuncs HUD_TxferLocalOverrides: gClientData = client.
  if (client) {
    ClientDataValue = *client;
    ClientData = &ClientDataValue;
  } else
    ClientData = nullptr;
}

float health() {
  return ClientData ? ClientData->health : 0.0f;
}

int waterlevel() {
  return ClientData ? ClientData->waterlevel : 0;
}

const float *punchangle() {
  return ClientData ? ClientData->punchangle : nullptr;
}

float fov() {
  return Fov;
}

} // namespace hudstate
