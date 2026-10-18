// modules::ModuleRegistry implementation - see Module.h.
#include "Module.h"

#include <abc/Console.h>

#include <cstring>

namespace modules {

ModuleRegistry &registry() {
  static ModuleRegistry registry;
  return registry;
}

void ModuleRegistry::add(IUiModule *module) {
  if (!module)
    return;
  for (size_t i = 0; i < Count; ++i) {
    if (Modules[i] == module) {
      abc::ConsoleWarn("[AbcUi] module '%s' registered twice, ignored\n",
                       module->name());
      return;
    }
  }
  if (Count >= kMaxModules) {
    abc::ConsoleWarn(
        "[AbcUi] module registry full (%zu), '%s' not registered\n",
        kMaxModules, module->name());
    return;
  }
  Modules[Count++] = module;
}

IUiModule *ModuleRegistry::find(const char *name) const {
  if (!name)
    return nullptr;
  for (size_t i = 0; i < Count; ++i) {
    if (std::strcmp(Modules[i]->name(), name) == 0)
      return Modules[i];
  }
  return nullptr;
}

void ModuleRegistry::initAll() {
  ShutDown = false;
  for (size_t i = 0; i < Count; i++)
    Modules[i]->onInit();
}

void ModuleRegistry::contextReadyAll(Rml::Context *context) {
  ShutDown = false;
  for (size_t i = 0; i < Count; i++)
    Modules[i]->onContextReady(context);
}

void ModuleRegistry::frameAll(double frameTime) {
  for (size_t i = 0; i < Count; i++)
    Modules[i]->onFrame(frameTime);
}

void ModuleRegistry::vidInitAll() {
  for (size_t i = 0; i < Count; i++)
    Modules[i]->onVidInit();
}

void ModuleRegistry::shutdownAll() {
  if (ShutDown)
    return;
  // Latch before dispatching: a re-entrant or late second teardown path
  // must not run module shutdown twice.
  ShutDown = true;
  for (size_t i = 0; i < Count; i++)
    Modules[i]->onShutdown();
}

} // namespace modules
