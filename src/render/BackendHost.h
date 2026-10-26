#pragma once

// src/render/BackendHost.h - the render-backend DI container (issue #24).
//
// The composition root: boot() runs once from OnLoadClient, selects the
// backend id from the archived abc_ui_render_backend cvar (default
// "renderer"), scans <mod>/abc/abcui/render_backend/*.dll, loads and
// validates every candidate, initializes the selected one and injects it
// through backend(). A missing or unusable selected backend is fatal
// (framework::fatalExit -> SysError); there is no built-in fallback, by
// design. Selection applies only after a full process restart.
//
// Read/registration split (the cvar red line): the value is READ at boot
// (CVAR_GET_POINTER is a lookup, and config.cfg-loaded values are visible
// that early, which is what makes the choice survive a restart) and
// REGISTERED in the HUD_Init one-shot block via registerCommands(), with
// the boot id as the default so an engine-created cvar keeps its value.

namespace abc::render {

class IRenderBackend;
class IWorldPass;
class IBlurPass;
class IOffscreenTargets;

// Composition root (see header note). Fatal on every failure mode in the
// issue's matrix; never returns with backend() == null.
void boot();
// Plugin teardown (OnShutdown): drops the frame sink and calls the
// descriptor's shutdown. The backend DLL stays loaded for the process
// lifetime - late teardown calls must stay safe, matching the old
// MetaRendererShutdown contract.
void shutdown();

// The injected backend (null only before boot / after shutdown).
IRenderBackend *backend();

// Capability conveniences: null when the backend is absent or does not
// support the capability - callers must route into their fallback branch.
IWorldPass *worldPass();
IBlurPass *blurPass();
IOffscreenTargets *targets();

// HUD_Init one-shot block: registers abc_ui_render_backend (ARCHIVE,
// default = the id this process booted) and abc_ui_render_backend_list.
void registerCommands();

// HUD_Redraw pump for RedrawBeat backends; no-op otherwise.
void pumpRedraw(double seconds);

} // namespace abc::render
