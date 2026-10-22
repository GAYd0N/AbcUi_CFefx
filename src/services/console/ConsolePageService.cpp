// services::console page access implementation - see ConsolePageService.h.
#include "ConsolePageService.h"

namespace services {
namespace console {
namespace {

PageAccess &slot() {
  static PageAccess instance;
  return instance;
}

} // namespace

void setPageAccess(const PageAccess &access) {
  slot() = access;
}

const PageAccess &pageAccess() {
  return slot();
}

} // namespace console
} // namespace services
