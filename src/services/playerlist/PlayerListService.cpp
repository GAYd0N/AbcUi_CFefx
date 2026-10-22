// services::playerlist access implementation - see PlayerListService.h.
#include "PlayerListService.h"

namespace services {
namespace playerlist {
namespace {

PlayerListModel *&slot() {
  static PlayerListModel *instance = nullptr;
  return instance;
}

} // namespace

void setModel(PlayerListModel *model) {
  slot() = model;
}

PlayerListModel *model() {
  return slot();
}

} // namespace playerlist
} // namespace services
