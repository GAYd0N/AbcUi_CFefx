// framework::PlayerListApi - the sdk playerlist section (the player-list
// page model). See ApiTable.h for how it joins the module table.
#include "ApiTable.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "../services/playerlist/PlayerListModel.h"
#include "../services/playerlist/PlayerListService.h"

namespace framework {
namespace PlayerListApi {
namespace {

void copyText(const std::string &text, char *out, uint32_t outSize) {
  if (!out || outSize == 0)
    return;
  uint32_t i = 0;
  for (; i + 1 < outSize && i < text.size(); ++i)
    out[i] = text[i];
  out[i] = '\0';
}

services::playerlist::PlayerListModel *model() {
  return services::playerlist::model();
}

void apiRefresh(void) {
  if (auto *target = model())
    target->refresh();
}

uint32_t apiRevision(void) {
  const auto *target = model();
  return target ? static_cast<uint32_t>(target->revision()) : 0;
}

int32_t apiRosterCount(void) {
  const auto *target = model();
  return target ? static_cast<int32_t>(target->roster().size()) : 0;
}

int32_t apiRosterAt(int32_t index, AbcUiPlayerRow *out) {
  const auto *target = model();
  if (!target || !out || index < 0)
    return 0;
  const auto &roster = target->roster();
  if (static_cast<size_t>(index) >= roster.size())
    return 0;
  const services::playerlist::RosterEntry &entry =
      roster[static_cast<size_t>(index)];

  AbcUiPlayerRow full{};
  full.structSize = sizeof(AbcUiPlayerRow);
  full.slot = entry.index;
  full.selected = target->selection().matches(entry) ? 1 : 0;
  uint32_t flags = 0;
  if (entry.local)
    flags |= ABCUI_PL_FLAG_LOCAL;
  if (entry.spectating)
    flags |= ABCUI_PL_FLAG_SPECTATING;
  if (entry.bot)
    flags |= ABCUI_PL_FLAG_BOT;
  if (entry.muted)
    flags |= ABCUI_PL_FLAG_MUTED;
  full.flags = flags;
  full.steamId64 = entry.steamId64;
  copyText(entry.name, full.name, sizeof(full.name));

  // The caller's declared size wins: never write past an older header.
  const uint32_t declared = out->structSize == 0
                                ? static_cast<uint32_t>(sizeof(full))
                                : out->structSize;
  std::memcpy(out, &full, std::min<size_t>(declared, sizeof(full)));
  return 1;
}

void apiSelect(uint64_t steamId64, int32_t slot, const char *name) {
  auto *target = model();
  if (!target)
    return;
  services::playerlist::RosterEntry probe;
  probe.index = slot;
  probe.steamId64 = steamId64;
  probe.name = name ? name : "";
  target->select(services::playerlist::SelectionKey::forEntry(probe));
}

int32_t apiMuteAvailable(void) {
  const auto *target = model();
  return target && target->muteAvailable() ? 1 : 0;
}

int32_t apiToggleMute(int32_t *mutedAfter) {
  auto *target = model();
  if (!target)
    return ABCUI_PL_TOGGLE_UNAVAILABLE;
  bool after = false;
  const auto result = target->toggleMuted(after);
  if (mutedAfter)
    *mutedAfter = after ? 1 : 0;
  switch (result) {
  case services::playerlist::ToggleError::kNone:
    return ABCUI_PL_TOGGLE_OK;
  case services::playerlist::ToggleError::kUnavailable:
    return ABCUI_PL_TOGGLE_UNAVAILABLE;
  case services::playerlist::ToggleError::kNoSelection:
    return ABCUI_PL_TOGGLE_NO_SELECTION;
  case services::playerlist::ToggleError::kStale:
    return ABCUI_PL_TOGGLE_STALE;
  case services::playerlist::ToggleError::kIdentityChanged:
    return ABCUI_PL_TOGGLE_IDENTITY_CHANGED;
  case services::playerlist::ToggleError::kLocalPlayer:
    return ABCUI_PL_TOGGLE_LOCAL_PLAYER;
  case services::playerlist::ToggleError::kReadbackFailed:
    return ABCUI_PL_TOGGLE_READBACK_FAILED;
  }
  return ABCUI_PL_TOGGLE_READBACK_FAILED;
}

const AbcUiPlayerListApi kPlayerListApi = {
    sizeof(AbcUiPlayerListApi),
    apiRefresh,
    apiRevision,
    apiRosterCount,
    apiRosterAt,
    apiSelect,
    apiMuteAvailable,
    apiToggleMute,
};

} // namespace

const AbcUiPlayerListApi *get() {
  return &kPlayerListApi;
}

} // namespace PlayerListApi
} // namespace framework
