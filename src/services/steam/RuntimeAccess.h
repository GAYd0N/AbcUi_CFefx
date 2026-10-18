#pragma once

// Shared read-only access to the host-owned Steam runtime. No Init, Shutdown
// or callback pumping. No cached interface lifetime leaks across HUD cycles.
namespace services::steam {
void *findUserInterface(const char *Version);
}
