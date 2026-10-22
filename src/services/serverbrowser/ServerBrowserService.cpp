// services::serverbrowser access implementation - see ServerBrowserService.h.
#include "ServerBrowserService.h"

namespace services {
namespace serverbrowser {
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

} // namespace serverbrowser
} // namespace services
