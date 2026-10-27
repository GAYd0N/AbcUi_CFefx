#pragma once

// src/render/BackendHost.h - the render-backend DI container (issue #24).
//
// The composition root: boot() runs once from OnLoadClient, selects the
// backend id from render_backend in abc/abcui/config.json, scans
// <mod>/abc/abcui/render_backend/*.dll and validates the candidates.
// Missing Renderer or its backend falls back to gl without rewriting the
// preference. Initialization failures remain fatal through SysError.
// Selection applies only after a full process restart.

namespace abc::render {

class IRenderBackend;
class IWorldPass;
class IBlurPass;
class IOffscreenTargets;

// Composition root (see header note). Fatal if selection, availability
// fallback or initialization fails; never returns with backend() == null.
void boot();
// Plugin teardown (OnShutdown): drops the frame sink and calls the
// descriptor's shutdown, then releases backend DLLs. All UI consumers must
// already be stopped; later backend() lookups return null.
void shutdown();

// The injected backend (null only before boot / after shutdown).
IRenderBackend *backend();

// Capability conveniences: null when the backend is absent or does not
// support the capability - callers must route into their fallback branch.
IWorldPass *worldPass();
IBlurPass *blurPass();
IOffscreenTargets *targets();

// HUD_Init one-shot block: registers abc_ui_render_backend_list.
void registerCommands();

// BaseUI post-paint pump for RedrawBeat backends; no-op otherwise.
void pumpRedraw(double seconds);

} // namespace abc::render
