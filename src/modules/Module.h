#pragma once

// modules - Ui-local feature-module lifecycle seam (T03). Modules self-
// register from their own translation unit; the spine (hooks.cpp +
// rmlui/RmlUiModule) only walks the registry at the lifecycle points below
// and never names a module. Adding a module is one .cpp under
// src/modules/<name>/ - no spine edit, no new header exports.
//
// The real spine order each event hangs off (T03 step-1 map, sources:
// plugins.cpp / hooks.cpp / RmlUiModule.cpp):
//
//   LoadClient      plugin entry only: interface grabs, sig scans, export
//                   chain. No cvar/command work (plan 08 §4.9 red line) and
//                   no module dispatch.
//   HUD_Init        every cycle: netmsg::init (usermsg chain installed once)
//                   + netmsg::refreshAfterClientLoad (re-chain only after a
//                   client DLL reload) and the HUD_Init panel install
//                   when the list is empty (HUD_Shutdown cleared it). The
//                   process-scoped block runs once: data-layer init ->
//                   PanelManager::registerPanelCvars ->
//                   rmlui::registerCommands -> registry().initAll(). The
//                   engine keeps registered cvars/commands across
//                   HUD_Shutdown cycles.
//   first HUD_Frame rmlui::init() assembly (per context): Rml::Initialise ->
//                   context -> fonts/localization/theme -> sprites/avatars/
//                   assets -> PanelManager::initAll (24 panels, factory
//                   order, untouched) -> debugger + render callbacks ->
//                   registry().contextReadyAll(context) as the LAST
//                   assembly step. DOM writes are only legal from
//                   onContextReady on.
//   HUD_Frame       PanelManager::onFrame -> registry().frameAll() ->
//                   Context::Update. Module frames run BEFORE the update
//                   pass so same-frame DOM edits are picked up.
//   HUD_Redraw      panel-only projection pump + second Context::Update;
//                   modules are not dispatched (no consumer yet).
//   HUD_VidInit     rmlui::onVidInit: sprite texture release -> theme scale
//                   + context resize -> PanelManager::vidInitAll ->
//                   registry().vidInitAll(). The early -windowed vid init
//                   (before the first HUD_Frame) returns before this tail,
//                   modules included - no vid event ever precedes
//                   contextReady.
//   HUD_Shutdown    rmlui::shutdown: render callbacks unregistered ->
//                   registry().shutdownAll() (modules release what they own
//                   while the context still exists) -> probe doc +
//                   PanelManager::shutdown -> context nulled ->
//                   Rml::Shutdown. shutdownAll is latched per init cycle;
//                   initAll or contextReadyAll re-arms it (both the
//                   HUD_Shutdown and ExitGame teardown paths funnel here).
//                   Before a context exists, shutdown still dispatches to
//                   modules armed by HUD_Init; before either init event it
//                   is a no-op.
//
// Deliberately NOT a module event: the early paint-hook installation T02
// observed (BaseUI::Initialize, ~1.3s before any context; popup coverage
// remains unproven). That stays a spine-owned explicit hook seam (rule 5);
// binding it to late onInit/contextReady misses the first frames.
//
// The registry stores raw pointers and owns nothing: each module is the
// static object of its own .cpp, which is its destructor owner (rule 11).

#include <cstddef>

namespace Rml {
class Context;
}

namespace modules {

class IUiModule {
public:
  virtual ~IUiModule() = default;

  virtual const char *name() const = 0;
  // HUD_Init-time guarded block: cvar/command registration only. The Rml
  // context does not exist yet - never touch DOM here.
  virtual void onInit() {}
  // The context exists; re-fired after every shutdown cycle (map change /
  // reconnect), because module documents die with each context and must be
  // rebuilt.
  virtual void onContextReady(Rml::Context *context) {
    (void)context;
  }
  // HUD_Frame, after the panel data pump, before Context::Update.
  virtual void onFrame(double frameTime) {
    (void)frameTime;
  }
  // HUD_VidInit, after the context resize and the panel resets.
  virtual void onVidInit() {}
  // While the context is still alive: release everything the module owns
  // (documents, listeners, borrowed handles). Not re-fired until the next
  // initAll or contextReadyAll.
  virtual void onShutdown() {}
};

class ModuleRegistry {
public:
  static constexpr size_t kMaxModules = 16;

  // Constant-initialized, so add() is safe from any static
  // initializer in the DLL regardless of translation-unit order.
  constexpr ModuleRegistry() = default;

  void add(IUiModule *module);
  // Debug/backend lookup by module name; nullptr when not registered yet.
  IUiModule *find(const char *name) const;

  void initAll();
  void contextReadyAll(Rml::Context *context);
  void frameAll(double frameTime);
  void vidInitAll();
  // Idempotent until initAll or contextReadyAll re-arms dispatch.
  void shutdownAll();

private:
  IUiModule *Modules[kMaxModules] = {};
  size_t Count = 0;
  bool ShutDown = true;
};

// Per-DLL singleton (function-local static; no static-init-order hazards).
ModuleRegistry &registry();

} // namespace modules
