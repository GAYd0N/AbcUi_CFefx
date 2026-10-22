// services::loading connection access implementation - see ConnectionService.h.
#include "ConnectionService.h"

namespace services {
namespace loading {
namespace {

ConnectionAccess &slot() {
  static ConnectionAccess instance;
  return instance;
}

} // namespace

void setConnectionAccess(const ConnectionAccess &access) {
  slot() = access;
}

const ConnectionAccess &connectionAccess() {
  return slot();
}

} // namespace loading
} // namespace services
