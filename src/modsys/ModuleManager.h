#pragma once

// framework::modmanager - the module package loader and lifecycle spine.
//
// boot() runs once per process from the HUD_Init path, before the panel
// install: it scans <mod>/abc/abcui/modules/*.zip, validates and resolves
// manifests (abi / minAbcUi / dependencies / conflicts, deterministically
// tie-broken by module id), memory-loads every entry DLL in dependency order
// and calls its AbcUiModule_Main entry. Descriptors join the framework
// lifecycle through a single registered IUiModule adapter, so loaded modules
// run at exactly the same spine points the built-in modules use.
//
// Entry/onLoad/onInit failures revoke owned registrations and retire the
// module together with its dependents. Callbacks are no-throw at the ABI;
// host exception guards are defensive, not transactional rollback. Teardown
// quiesces every module before reverse-order unload (after Rml shutdown).

#include <memory>
#include <vector>

#include "ModulePanel.h"

namespace framework {

namespace modmanager {

// One-shot per process; safe to call on every HUD_Init cycle. Returns the
// number of modules still loaded (retired initialization failures excluded).
size_t boot();

// Lifecycle forwarders, driven by the registry adapter. contextReady
// loads every registered module page before the modules see the context.
void initAll();
void contextReadyAll(Rml::Context *context);
void frameAll(double frameTime);
void vidInitAll();
void shutdownAll();

// Process teardown: onUnload in reverse load order, then the images and the
// VFS mounts go away.
void unloadAll();

// Panel registration during a module's onLoad (ApiTable forwards here).
// Refused outside a load window or on a duplicate panel name.
bool acceptPanel(const AbcUiPanelDesc &desc);

// Panels registered during boot, in registration order. The descriptors are
// kept (onLoad runs once per process); every HUD_Init install cycle gets
// fresh wrappers over them, because HUD_Shutdown destroys the panel list.
std::vector<std::unique_ptr<HudPanel>> takePanels();

} // namespace modmanager

} // namespace framework
