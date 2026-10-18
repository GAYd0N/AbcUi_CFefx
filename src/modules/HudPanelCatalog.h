#pragma once

// modules::HudPanelCatalog - the ordered HUD panel composition table.
//
// PanelManager (framework) owns the registry and the pump, but it must not
// know the concrete panel types: the catalog is the one place that includes
// every panel header and fixes the construction/registration order. The
// lifecycle calls installHudPanels() once, before PanelManager::initAll().

namespace modules {

// Builds the ordered panel list and hands ownership to PanelManager. Safe to
// call once per HUD_Init cycle; a second call replaces the list.
void installHudPanels();

// HUD_Init-time cvar/command registration for the batch panels. Panel
// namespaces own their cvar pointers; this only triggers the batch in order.
void registerHudPanelCvars();

} // namespace modules
