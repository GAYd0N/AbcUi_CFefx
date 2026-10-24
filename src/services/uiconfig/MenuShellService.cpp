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

std::string &rootSlot() {
  static std::string instance;
  return instance;
}
void setResourceRoot(std::string uri) {
  rootSlot() = std::move(uri);
}
const std::string &resourceRoot() {
  return rootSlot();
}

void setShellAccess(const MenuShellAccess &access) {
  slot() = access;
}

const MenuShellAccess &shellAccess() {
  return slot();
}

} // namespace uiconfig
} // namespace services
