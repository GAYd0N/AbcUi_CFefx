// services::inventory access implementation - see InventoryService.h.
#include "InventoryService.h"

namespace services {
namespace inventory {
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

} // namespace inventory
} // namespace services
