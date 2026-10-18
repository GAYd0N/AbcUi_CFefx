#pragma once

// data::playerres - the 32-slot player info store (ScoreInfo wire values +
// engine studio PlayerInfo mirror). from: ABCEnchance/src/core/resource/
// playerresource.*.
//
// Backend notes (user directive: the steamapi layer is swappable - see
// rmlui/SteamAPI.h): the SteamID64 column comes from the engine's
// player_info_sc_t::m_nSteamID for every slot, with the local slot falling
// back to steamapi::localSteamId(). The persona-name column rides
// steamapi::personaName() (old GetFriendPersonaName) - empty while the
// active backend cannot serve names.
//
// Donor rides the client dll's native HUD panel info table (sig-located
// by the spine, see hooks.h) exactly like the old g_aryNativeHUDPanelInfo;
// admin comes off the ScoreInfo wire byte.

#include <string>

#include "TeamResource.h"

#include <cstdint>

struct player_info_s;

namespace playerres {

constexpr int kMaxPlayers = 32;

// Old PlayerInfo::ADMIN.
enum class Admin : int {
  None = 0,
  Operator,
  ServerOwner,
};

// Old PlayerInfo::HIDE_EXTRA (ClExtras-provided hide level).
enum class HideExtra : int {
  No = 0,
  HideAdmin = 1,
  HideOwner = 2,
};

// Old PlayerInfo::DONOR - the base is the wire value the client dll's native
// HUD panel table stores (0x10000 + kind); the cast is direct.
enum class Donor : int {
  None = 65536,
  ElectricCrowbar,
  GoldedUzi,
  GoldedDollar,
  Tester,
  Artist,
  Developer,
};

struct PlayerInfo {
  int index = 0; // 1..32 (entindex)
  int ping = 0;
  int loss = 0;
  std::string model;
  std::string name;
  std::string realName;
  int topColor = 0;
  int bottomColor = 0;
  float frags = 0.0f;
  int deaths = 0;
  float health = 0.0f;
  float armor = 0.0f;
  teamres::TeamId team = teamres::TeamId::None;
  Admin admin = Admin::None;
  HideExtra hideExtra = HideExtra::No;
  Donor donor = Donor::None;
  bool connected = false;
  bool spectate = false;
  std::uint64_t steamId64 = 0;
#ifndef NDEBUG
  // abc_ui_board instrument: the slot identity was injected by the Debug
  // acceptance driver - update() keeps it instead of the empty engine
  // mirror (a local game has no real players in slots 2..32).
  bool debugPinned = false;
#endif

  // Old IsValid: connected AND named.
  bool isValid() const {
    return connected && !name.empty();
  }
  bool isThisPlayer() const;
  // Re-reads the engine mirror (studio PlayerInfo) + native donor/admin.
  void update();
  void reset();
};

// HUD_Init-time: subscribes the ScoreInfo/Spectator/ClExtras feed. Idempotent.
void init();
// Map transition (HUD_VidInit chain).
void resetAll();
// ClExtras / periodic refresh: re-reads every engine mirror.
void updateAll();

// idx is the entindex 0..32 (0/null -> nullptr), old GetPlayerInfo.
PlayerInfo *getPlayerInfo(int idx);
PlayerInfo *getLocalPlayerInfo();

// Old PlayerResource::IsInSpectate: the entity's iuser1 observer flag.
bool isEntitySpectate(int idx);

#ifndef NDEBUG
// abc_ui_board instrument: fill slots 2..32 with fake identities (mixed
// teams/frags/health) so a local session can drive the scoreboard/table
// interactions at full occupancy. Debug builds only.
void debugPopulateBoard();
#endif

} // namespace playerres
