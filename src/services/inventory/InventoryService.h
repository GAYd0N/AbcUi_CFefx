#pragma once

// services::inventory::access - the sdk inventory section's access point:
// the process-lifetime item model plus the flow's dismiss. The model is
// fed by the InvAdd/InvRemove stream (the composition root wires the feed
// once); the actions' client commands run framework-side (the native
// order: the window closes, then the command runs).

namespace services {
namespace inventory {

class InventoryModel;

struct Access {
  InventoryModel *model = nullptr;
  void (*dismiss)() = nullptr;
};

void setAccess(const Access &access);
const Access &access();

} // namespace inventory
} // namespace services
