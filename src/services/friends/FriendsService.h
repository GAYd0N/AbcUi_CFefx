#pragma once

// services::friends::join seam - the sdk social section's join intent. The
// friends board model is the process singleton (friendsModel()); the social
// section reads it directly. Only the join intent needs a seam: the
// composition root validates the address with the direct-connect grammar
// and issues the connect attempt.

#include <cstdint>

namespace services {
namespace friends {

struct JoinSeam {
  void (*join)(uint64_t steamId, const char *address) = nullptr;
};

void setJoinSeam(const JoinSeam &seam);
const JoinSeam &joinSeam();

} // namespace friends
} // namespace services
