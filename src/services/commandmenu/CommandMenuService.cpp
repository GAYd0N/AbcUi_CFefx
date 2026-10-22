// services::commandmenu access implementation - see CommandMenuService.h.
#include "CommandMenuService.h"

namespace services {
namespace commandmenu {
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

} // namespace commandmenu
} // namespace services
