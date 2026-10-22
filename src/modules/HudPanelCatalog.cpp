// modules::HudPanelCatalog implementation - see HudPanelCatalog.h.
#include "HudPanelCatalog.h"

#include "../framework/Commands.h"
#include "../framework/HudPanel.h"
#include "../framework/PanelManager.h"
#include "../modsys/ModuleManager.h"

#include <utility>
#include <vector>

namespace modules {

void installHudPanels() {
  // Every HUD panel is a UI module now, so the cycle is exactly the
  // module-registered set (in package load order). Installing a fresh list
  // per HUD_Init is what re-arms the boards after HUD_Shutdown cleared the
  // previous one; the framework keeps no built-in panel factory.
  framework::PanelManager::installPanels(framework::modmanager::takePanels());
}

void registerHudPanelCvars() {
  // Process-scoped registration that stays in the framework spine: the
  // chained commands every module subscribes to.
  commands::registerAll();
}

} // namespace modules
