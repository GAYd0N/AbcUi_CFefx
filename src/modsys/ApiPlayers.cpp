// framework::PlayersApi - the sdk players section (player-resource rows and
// engine entity snapshots). See ApiTable.h for how it joins the module table.
#include "ApiTable.h"
#include "ApiSnapshot.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "../data/PlayerResource.h"
#include "../rmlui/texture/Avatars.h"
#include "../hooks.h"

namespace framework {
namespace PlayersApi {
namespace {

void copyField(const std::string &text, char *out, size_t outSize) {
  if (!out || outSize == 0)
    return;
  size_t i = 0;
  for (; i + 1 < outSize && i < text.size(); ++i)
    out[i] = text[i];
  out[i] = '\0';
}

void fill(const playerres::PlayerInfo &info, void *out) {
  AbcUiPlayerInfo full{};
  full.structSize = sizeof(AbcUiPlayerInfo);
  full.index = info.index;
  full.valid = info.isValid() ? 1 : 0;
  full.spectate = info.spectate ? 1 : 0;
  full.team = static_cast<int32_t>(info.team);
  full.admin = static_cast<int32_t>(info.admin);
  full.donor = static_cast<int32_t>(info.donor);
  full.topColor = info.topColor;
  full.bottomColor = info.bottomColor;
  full.ping = info.ping;
  full.loss = info.loss;
  full.frags = info.frags;
  full.health = info.health;
  full.armor = info.armor;
  full.steamId64 = info.steamId64;
  copyField(info.name, full.name, sizeof(full.name));
  copyField(info.model, full.model, sizeof(full.model));
  full.deaths = info.deaths;
  copyField(info.realName, full.realName, sizeof(full.realName));
  abi::copySnapshot(out, full);
}

void apiLocal(AbcUiPlayerInfo *out) {
  if (!out)
    return;
  const playerres::PlayerInfo *info = playerres::getLocalPlayerInfo();
  if (!info) {
    AbcUiPlayerInfo empty{};
    empty.structSize = sizeof(AbcUiPlayerInfo);
    abi::copySnapshot(out, empty);
    return;
  }
  fill(*info, out);
}

int32_t apiGet(int32_t index, AbcUiPlayerInfo *out) {
  if (!out)
    return 0;
  const playerres::PlayerInfo *info = playerres::getPlayerInfo(index);
  if (!info)
    return 0;
  fill(*info, out);
  return 1;
}

int32_t apiEntity(int32_t index, AbcUiEntityState *out) {
  if (!out)
    return 0;
  AbcUiEntityState state{};
  state.structSize = sizeof(AbcUiEntityState);
  const cl_entity_t *entity = gEngfuncs.GetEntityByIndex(index);
  if (entity) {
    state.valid = 1;
    state.messageNum = entity->curstate.messagenum;
    state.isPlayer = entity->player ? 1 : 0;
    state.effects = entity->curstate.effects;
    for (int i = 0; i < 3; ++i) {
      state.origin[i] = entity->curstate.origin[i];
      state.angles[i] = entity->curstate.angles[i];
      state.mins[i] = entity->curstate.mins[i];
      state.maxs[i] = entity->curstate.maxs[i];
    }
  }
  abi::copySnapshot(out, state);
  return state.valid;
}

int32_t apiClientColor(int32_t index, float out[3]) {
  if (!out || !hooks::GetClientColor)
    return 0;
  const float *tint = hooks::GetClientColor(index);
  if (!tint)
    return 0;
  out[0] = std::clamp(tint[0], 0.0f, 1.0f);
  out[1] = std::clamp(tint[1], 0.0f, 1.0f);
  out[2] = std::clamp(tint[2], 0.0f, 1.0f);
  return 1;
}

int32_t apiAvatar(uint64_t steamId64, int32_t *generation) {
  if (!generation)
    return ABCUI_ERROR_INVALID;
  int value = 0;
  const int result = avatars::status(steamId64, value);
  *generation = value;
  return result;
}

int32_t apiAvatarAnimated(uint64_t steamId64, int32_t *generation) {
  if (!generation)
    return ABCUI_ERROR_INVALID;
  int value = 0;
  const int result = avatars::animatedStatus(steamId64, value);
  *generation = value;
  return result;
}

int32_t apiTeamNameToken(int32_t team, char *out, uint32_t outSize) {
  const char *token =
      teamres::teamNameToken(static_cast<teamres::TeamId>(team));
  const std::string text = token ? token : "";
  if (out && outSize > 0) {
    size_t i = 0;
    for (; i + 1 < outSize && i < text.size(); ++i)
      out[i] = text[i];
    out[i] = 0;
  }
  return static_cast<int32_t>(text.size());
}

void apiRefresh(int32_t full) {
  if (full)
    playerres::updateAll();
  else
    playerres::refreshPingLossAll();
}

const AbcUiPlayersApi kPlayersApi = {
    sizeof(AbcUiPlayersApi), apiLocal,  apiGet,           apiEntity,
    apiClientColor,          apiAvatar, apiTeamNameToken, apiRefresh,
    apiAvatarAnimated,
};

} // namespace

const AbcUiPlayersApi *get() {
  return &kPlayersApi;
}

} // namespace PlayersApi
} // namespace framework
