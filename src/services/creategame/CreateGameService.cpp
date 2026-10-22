// services::creategame access implementation - see CreateGameService.h.
#include "CreateGameService.h"

namespace services {
namespace creategame {
namespace {

Access &slot() {
  static Access instance;
  return instance;
}

} // namespace

void setAccess(const Access &access) {
  slot() = access;
}

const Access &access() {
  return slot();
}

} // namespace creategame
} // namespace services
