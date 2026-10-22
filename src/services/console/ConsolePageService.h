#pragma once

// services::console::page access - the sdk consolepage section's close
// funnel. The console buffer/backend are process singletons (the section
// reads them directly); only the close funnel needs a seam: it lowers the
// bridge truth AND the engine-side console flag (issue #5), and both edges
// belong to the composition root.

namespace services {
namespace console {

struct PageAccess {
  void (*requestClose)() = nullptr;
};

void setPageAccess(const PageAccess &access);
const PageAccess &pageAccess();

} // namespace console
} // namespace services
