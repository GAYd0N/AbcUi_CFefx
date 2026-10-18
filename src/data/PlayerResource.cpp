// data::playerres implementation - see PlayerResource.h.
// from: ABCEnchance/src/core/resource/playerresource.cpp (GBK->UTF-8).
// Studio mirror reads go through hooks::EngineStudioApi (captured from the
// HUD_GetStudioModelInterface chain); donor/admin ride the client's native
// HUD panel table (hooks::NativeHudPanelInfo).
#include <metahook.h>

#include "PlayerResource.h"

#include <abc/Console.h>
#include <com_model.h>
#include <r_studioint.h>

#include <algorithm>
#include <cstdio>

#include "../hooks.h"
#include "../services/steam/SteamAPI.h"
#include "NetworkMessages.h"

namespace playerres {

namespace {

std::array<PlayerInfo, kMaxPlayers + 1> &infos() {
  static std::array<PlayerInfo, kMaxPlayers + 1> Infos = []() {
    std::array<PlayerInfo, kMaxPlayers + 1> array{};
    for (int i = 0; i <= kMaxPlayers; i++)
      array[i].index = i;
    return array;
  }();
  return Infos;
}

// Old PlayerInfo::Update donor read: the native HUD panel table carries the
// donor/admin columns the wire omits.
void readNativeFlags(PlayerInfo &info) {
  int32_t donor = 0;
  info.donor = (info.hideExtra != HideExtra::No &&
                hooks::NativeHudPanelInfo.readDonor(info.index, donor))
                   ? static_cast<Donor>(donor)
                   : Donor::None;
  // ADMIN rides the wire (ScoreInfo admin byte); the native table's admin
  // column stayed unread by the old code - same here.
}

} // namespace

bool PlayerInfo::isThisPlayer() const {
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  return local ? index == local->index : false;
}

void PlayerInfo::update() {
#ifndef NDEBUG
  if (debugPinned)
    return;
#endif
  if (!hooks::EngineStudioApi)
    return;
  const auto *info = reinterpret_cast<const player_info_sc_t *>(
      hooks::EngineStudioApi->PlayerInfo(index - 1));
  const bool wasConnected = connected;
  connected = info && info->name[0];
  if (connected && steamId64 && info->m_nSteamID &&
      steamId64 != info->m_nSteamID) {
    reset();
    connected = true;
  }
  if (!connected) {
    reset();
    return;
  }
  if (connected != wasConnected) {
    // Player connected or disconnected: drop the stale identity fields
    // (the old g_EventPlayerInfoChanged subscribers saw this state too).
    name.clear();
    realName.clear();
    model.clear();
    steamId64 = 0;
  }
  if (connected) {
    if (steamId64 == 0 || info->m_nSteamID != steamId64) {
      // Old order: the LOCAL slot unconditionally took
      // SteamUser()->GetSteamID() (the engine column can carry a
      // bot/placeholder id for slot 1); everyone else the engine value.
      const auto *local = gEngfuncs.GetLocalPlayer();
      if (local && index == local->index) {
        steamId64 = steamapi::localSteamId();
        if (!steamId64)
          steamId64 = info->m_nSteamID;
      } else {
        steamId64 = info->m_nSteamID;
      }
      // Old real-name read (GetFriendPersonaName); the active steamapi
      // backend answers, empty when it cannot serve names.
      realName = steamId64 ? steamapi::personaName(steamId64) : std::string();
    } else if (steamId64 && realName.empty()) {
      // The persona surface can come up after the first read (late
      // steam init) - re-ask while the column is empty (old code's
      // lazy SteamFriends() accessor had the same effect).
      realName = steamapi::personaName(steamId64);
    }
    topColor = info->topcolor;
    bottomColor = info->bottomcolor;
    ping = info->ping;
    loss = info->packet_loss;
    name = info->name;
    model = info->model;
    readNativeFlags(*this);
  }
}

void PlayerInfo::reset() {
#ifndef NDEBUG
  debugPinned = false;
#endif
  ping = 0;
  loss = 0;
  model.clear();
  name.clear();
  realName.clear();
  topColor = 0;
  bottomColor = 0;
  frags = 0.0f;
  deaths = 0;
  health = 0.0f;
  armor = 0.0f;
  team = teamres::TeamId::None;
  admin = Admin::None;
  connected = false;
  spectate = false;
  hideExtra = HideExtra::No;
  donor = Donor::None;
  steamId64 = 0;
}

void init() {
  static bool registered = false;
  if (registered)
    return;
  registered = true;
  netmsg::ScoreInfo.append([](int index, float frag, int death, float health,
                              float armor, int team, int extra, int admin) {
    PlayerInfo *info = getPlayerInfo(index);
    if (!info)
      return;
    info->frags = frag;
    info->deaths = death;
    info->health = health;
    info->armor = armor;
    info->team = static_cast<teamres::TeamId>(team);
    info->hideExtra = static_cast<HideExtra>(extra);
    info->admin = static_cast<Admin>(admin);
    info->update();
  });
  netmsg::Spectator.append([](int index, bool spectate) {
    PlayerInfo *info = getPlayerInfo(index);
    if (!info)
      return;
    info->spectate = spectate;
  });
  // ClExtrasInfo marks "donor/extra data changed" (the payload was never
  // parsed - old code just re-read the mirrors).
  netmsg::ClExtrasInfo.append([]() { updateAll(); });
}

void resetAll() {
  for (auto &info : infos())
    info.reset();
}

void updateAll() {
  for (auto &info : infos()) {
    if (info.index >= 1)
      info.update();
  }
}

PlayerInfo *getPlayerInfo(int idx) {
  if (idx < 0 || idx > kMaxPlayers)
    return nullptr;
  return &infos()[idx];
}

PlayerInfo *getLocalPlayerInfo() {
  const cl_entity_t *ent = gEngfuncs.GetLocalPlayer();
  if (ent)
    return getPlayerInfo(ent->index);
  return nullptr;
}

bool isEntitySpectate(int idx) {
  const cl_entity_t *plr = gEngfuncs.GetEntityByIndex(idx);
  return plr ? plr->curstate.iuser1 > 0 : false;
}

#ifndef NDEBUG

void debugPopulateBoard() {
  // 31 fake teammates/opponents for the full-occupancy scoreboard runs:
  // two teams, spread frags/deaths/hp/armor/ping, a few spectators, donor
  // and admin coverage. Slot 1 stays the real local player.
  static const char *kNames[] = {
      "Alpha_Commando",  "Bravo_Hunter",  "Charlie_Sniper", "Delta_Medic",
      "Echo_Engineer",   "Foxtrot_Scout", "Golf_Heavy",     "Hotel_Recon",
      "India_Grenadier", "Juliet_Point",  "Kilo_Rifleman",  "Limo_Driver",
      "Mike_Support",    "November_Ops",  "Oscar_Pilot",    "Papa_Gunner",
      "Quebec_Lurker",   "Romeo_Blade",   "Sierra_Fix",     "Tango_Unit",
      "Uniform_Wolf",    "Victor_Crane",  "Whiskey_Tac",    "Xray_Node",
      "Yankee_Foxtrot",  "Zulu_Rampart",  "Grunt_TwoSeven", "Raider_Nine",
      "Warden_Four",     "Cinder_Twelve", "Nomad_Sixteen",
  };
  for (int index = 2; index <= kMaxPlayers; index++) {
    PlayerInfo &info = infos()[index];
    info.reset();
    const int n = index - 2;
    info.index = index;
    info.name = kNames[n];
    info.connected = true;
    info.model = "gordon";
    info.team = (n % 2 == 0) ? teamres::TeamId::Team1 : teamres::TeamId::Team2;
    info.frags = static_cast<float>((n * 7) % 40) - 5.0f;
    info.deaths = (n * 3) % 25;
    info.health = static_cast<float>((n * 13) % 130) - 15.0f;
    info.armor = static_cast<float>((n * 11) % 120) - 10.0f;
    info.ping = (n * 17) % 260;
    info.loss = n % 5;
    info.spectate = (n % 11) == 3;
    info.steamId64 = 76561197960287930ull + static_cast<std::uint64_t>(index);
    info.hideExtra = (n % 4 == 0) ? HideExtra::HideAdmin : HideExtra::No;
    info.admin = (n == 2) ? Admin::ServerOwner
                          : ((n == 4) ? Admin::Operator : Admin::None);
    info.donor = static_cast<Donor>(65536 + (n % 7));
    info.debugPinned = true;
  }
  netmsg::ServerName("AbcUi board instrument (fake full server)");
  netmsg::TimeEnd(1234);
  abc::ConsolePrint("[AbcUi] board: 31 fake players pinned (Team1/Team2, "
                    "spectators, donor/admin spread)\n");
}

#endif

} // namespace playerres
