// services::uiconfig shell access implementation - see MenuShellService.h.
#include "MenuShellService.h"

namespace services {
namespace uiconfig {
namespace {

MenuShellAccess &slot() {
  static MenuShellAccess instance;
  return instance;
}

} // namespace

void setShellAccess(const MenuShellAccess &access) {
  slot() = access;
}

const MenuShellAccess &shellAccess() {
  return slot();
}

} // namespace uiconfig
} // namespace services
