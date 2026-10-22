// services::votemenu access implementation - see VoteMenuService.h.
#include "VoteMenuService.h"

namespace services {
namespace votemenu {
namespace {

VoteMenuModel *&slot() {
  static VoteMenuModel *instance = nullptr;
  return instance;
}

} // namespace

void setModel(VoteMenuModel *model) {
  slot() = model;
}

VoteMenuModel *model() {
  return slot();
}

} // namespace votemenu
} // namespace services
