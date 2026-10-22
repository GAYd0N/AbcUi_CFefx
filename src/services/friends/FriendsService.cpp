// services::friends join seam implementation - see FriendsService.h.
#include "FriendsService.h"

namespace services {
namespace friends {
namespace {

JoinSeam &slot() {
  static JoinSeam instance;
  return instance;
}

} // namespace

void setJoinSeam(const JoinSeam &seam) {
  slot() = seam;
}

const JoinSeam &joinSeam() {
  return slot();
}

} // namespace friends
} // namespace services
