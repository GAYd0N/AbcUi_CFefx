// framework::SocialApi - the sdk social section (the shell's friends
// board). See ApiTable.h for how it joins the module table.
#include "ApiTable.h"

#include <algorithm>
#include <cstring>

#include "../services/friends/FriendsModel.h"
#include "../services/friends/FriendsService.h"

namespace framework {
namespace SocialApi {
namespace {

void copyText(const std::string &text, char *out, uint32_t outSize) {
  if (!out || outSize == 0)
    return;
  uint32_t i = 0;
  for (; i + 1 < outSize && i < text.size(); ++i)
    out[i] = text[i];
  out[i] = '\0';
}

services::friends::FriendsModel &model() {
  return services::friends::friendsModel();
}

uint32_t apiGeneration(void) {
  return static_cast<uint32_t>(model().generation());
}

int32_t apiSteamAvailable(void) {
  return model().available() ? 1 : 0;
}

int32_t apiRowCount(void) {
  return static_cast<int32_t>(model().rows().size());
}

int32_t apiRowAt(int32_t index, AbcUiFriendRow *out) {
  const auto &rows = model().rows();
  if (!out || index < 0 || static_cast<size_t>(index) >= rows.size())
    return 0;
  const services::friends::FriendEntry &row = rows[static_cast<size_t>(index)];

  AbcUiFriendRow full{};
  full.structSize = sizeof(AbcUiFriendRow);
  full.steamId = row.SteamId;
  // Row 0 is the local player by the model's row order.
  full.self = index == 0 ? 1 : 0;
  full.inGame = row.InGame ? 1 : 0;
  full.otherGame = row.OtherGame ? 1 : 0;
  copyText(row.Name, full.name, sizeof(full.name));
  copyText(row.Game, full.game, sizeof(full.game));
  copyText(row.Connect, full.connect, sizeof(full.connect));

  // The caller's declared size wins: never write past an older header.
  const uint32_t declared = out->structSize == 0
                                ? static_cast<uint32_t>(sizeof(full))
                                : out->structSize;
  std::memcpy(out, &full, std::min<size_t>(declared, sizeof(full)));
  return 1;
}

void apiJoin(uint64_t steamId, const char *address) {
  const services::friends::JoinSeam &seam = services::friends::joinSeam();
  if (seam.join)
    seam.join(steamId, address ? address : "");
}

const AbcUiSocialApi kSocialApi = {
    sizeof(AbcUiSocialApi),
    apiGeneration,
    apiSteamAvailable,
    apiRowCount,
    apiRowAt,
    apiJoin,
};

} // namespace

const AbcUiSocialApi *get() {
  return &kSocialApi;
}

} // namespace SocialApi
} // namespace framework
