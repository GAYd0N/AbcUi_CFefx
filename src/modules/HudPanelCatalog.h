#pragma once

// modules::HudPanelCatalog - the ordered HUD panel composition table.
//
// PanelManager (framework) owns the registry and the pump, but it must not
// know the concrete panel types. Every HUD panel is now a UI module, so this
// catalog is thin: installHudPanels() hands the module-registered panels to
// the manager and registerHudPanelCvars() keeps the process-scoped command
// registration the spine still owns. The lifecycle calls installHudPanels()
// once per HUD_Init, before PanelManager::initAll().

namespace modules {

// Hands the module-registered panels to PanelManager (fresh list per
// HUD_Init cycle; a second call replaces the list).
void installHudPanels();

// HUD_Init-time process-scoped registration (the chained commands the
// modules subscribe to).
void registerHudPanelCvars();

} // namespace modules
