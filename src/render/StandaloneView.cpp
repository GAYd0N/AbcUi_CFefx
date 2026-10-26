// framework::standaloneview implementation - see StandaloneView.h.
//
// Route copied from MetaHookSv/StandaloneView (MIT, (c) 2021 hzqst); see
// that project's src/StandaloneViewManager.cpp, src/privatehook.cpp and
// src/EmptyWorld.cpp for the original shape. AbcUi adaptations: soft
// gamedata degradation instead of SysError, the drawfuncs surface is
// studio-only, and the client-export routing goes through hooks.cpp's
// chain (gExportfuncs) rather than a saved bridge.
#include <metahook.h>

#include <com_model.h>
#include <const.h>
#include <demo_api.h>
#include <r_studioint.h>
#include <ref_params.h>
#include <studio.h>

#include <abc/Console.h>
#include <abc/CvarCmd.h>
#include <abc/Plugin.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "StandaloneView.h"

namespace framework {
namespace standaloneview {

namespace {

// cactive_t::ca_active and SIGNONS, the values V_RenderView requires.
constexpr int ClientStateActive = 5;
constexpr int SignonsComplete = 2;

// ---------------------------------------------------------------- bridge --

// Engine functions and globals resolved from gamedata.
struct EngineBridge {
  // The original V_RenderView is read per call through OriginalRenderView
  // (MetaHook writes the trampoline only after every plugin's LoadClient
  // returned; caching it at install time stored NULL).
  int *clientState = nullptr;         // cls.state
  int *clientSignon = nullptr;        // cls.signon
  model_s **worldModel = nullptr;     // cl_worldmodel
  cl_entity_s *worldEntity = nullptr; // r_worldentity
  int (*CL_FxBlend)(cl_entity_s *ent) = nullptr;
  cl_entity_s **currententity = nullptr;
  float *r_blend = nullptr;
};

EngineBridge Engine;
bool EngineHooksInstalled = false;
bool Available = false;
bool ClientHooksReady = false;

// The engine's r_studio_interface_t and engine studio API, captured from
// HUD_GetStudioModelInterface (hooks.cpp forwards them).
r_studio_interface_t **StudioInterface = nullptr;
engine_studio_api_t *EngineStudio = nullptr;

hook_t *RenderViewHook = nullptr;
hook_t *PaintBackgroundHook = nullptr;
hook_t *RenderPortalsHook = nullptr;
hook_t *ResetAllHook = nullptr;
hook_t *ThirdPersonHook = nullptr;
void (*OriginalRenderView)() = nullptr; // read per call, NOT cached in a
                                        // bridge: MetaHook writes the
                                        // trampoline only after every
                                        // plugin's LoadClient returned
using PaintBackgroundFn = void (*)(int *extents);
PaintBackgroundFn OriginalPaintBackground = nullptr;
using RenderPortalsFn = void(__fastcall *)(void *pthis, int dummy,
                                           ref_params_s *pparams);
RenderPortalsFn OriginalRenderPortals = nullptr;
using ResetAllFn = int(__fastcall *)(void *pthis, int dummy);
ResetAllFn OriginalResetAll = nullptr;
int (*OriginalThirdPerson)() = nullptr;

// --------------------------------------------------------------- views ----

std::vector<View *> &views() {
  static std::vector<View *> instance;
  return instance;
}

View *CurrentView = nullptr;
bool RenderingViews = false;
bool DrawingViewTriangles = false;
int PaintBackgroundDepth = 0;

class StudioDrawfuncs final : public Drawfuncs {
public:
  bool setupStudioPose(cl_entity_s *ent, cl_entity_s *boneSource) override;
  void drawStudioModel(cl_entity_s *ent, cl_entity_s *boneSource,
                       const StudioLighting *lighting) override;
};

StudioDrawfuncs DrawfuncsInstance;

// Restores cls.state / cls.signon after the forced-active scope.
class ClientStateScope {
public:
  ClientStateScope(int *clientState, int *clientSignon)
      : state(clientState), signon(clientSignon), savedState(*clientState),
        savedSignon(*clientSignon) {
    *state = ClientStateActive;
    *signon = SignonsComplete;
  }
  ~ClientStateScope() {
    *state = savedState;
    *signon = savedSignon;
  }
  ClientStateScope(const ClientStateScope &) = delete;
  ClientStateScope &operator=(const ClientStateScope &) = delete;

private:
  int *state;
  int *signon;
  int savedState;
  int savedSignon;
};

// ------------------------------------------------------------ empty world -

constexpr char kEmptyWorldName[] = "*abcui_empty";

mleaf_t EmptyLeaf = {};
model_t EmptyWorld = {};
bool EmptyWorldInitialized = false;

model_t *emptyWorldModel() {
  if (EmptyWorldInitialized)
    return &EmptyWorld;
  EmptyLeaf.contents = CONTENTS_EMPTY;
  strncpy(EmptyWorld.name, kEmptyWorldName, sizeof(EmptyWorld.name) - 1);
  EmptyWorld.type = mod_brush;
  // Mod_PointInLeaf stops at the root because it is a leaf; Mod_LeafPVS
  // returns mod_novis for leafs[0]; numleafs = 0 leaves nothing for
  // R_MarkLeaves to mark.
  EmptyWorld.nodes = reinterpret_cast<mnode_t *>(&EmptyLeaf);
  EmptyWorld.leafs = &EmptyLeaf;
  EmptyWorldInitialized = true;
  return &EmptyWorld;
}

// Installs the empty worldmodel unless the client has a fully loaded one;
// restores both pointers afterwards. Any frame of a non-active client uses
// the empty world: CL_RegisterResources and R_NewMap set cl.worldmodel and
// r_worldentity.model at different times, and a pass in that window would
// hit a NULL worldmodel.
class EmptyWorldScope {
public:
  EmptyWorldScope(model_s **worldModel, cl_entity_s *worldEntity,
                  bool worldReady)
      : worldModelSlot(worldModel), worldEntity(worldEntity) {
    if (!worldModelSlot || !worldEntity)
      return;
    if (worldReady && *worldModelSlot && worldEntity->model)
      return;
    savedWorldModel = *worldModelSlot;
    savedEntityModel = worldEntity->model;
    *worldModelSlot = emptyWorldModel();
    worldEntity->model = *worldModelSlot;
    installed = true;
    // The empty world carries no BSP visibility data, so the engine's leaf/PVS
    // test culls the preview entity and the pass draws nothing (the client's
    // StudioDrawModel never reaches the model renderer). r_novis takes the
    // no-visibility path for the duration of the pass; the value rides the
    // world swap, so the main view sees the player's own setting again.
    if (cvar_t *noVis = abc::CVAR_GET_POINTER("r_novis")) {
      noVisCvar = noVis;
      savedNoVis = noVis->value;
      noVis->value = 1.0f;
    }
  }
  ~EmptyWorldScope() {
    if (noVisCvar) {
      noVisCvar->value = savedNoVis;
      noVisCvar = nullptr;
    }
    if (!installed)
      return;
    *worldModelSlot = savedWorldModel;
    worldEntity->model = savedEntityModel;
  }
  EmptyWorldScope(const EmptyWorldScope &) = delete;
  EmptyWorldScope &operator=(const EmptyWorldScope &) = delete;

private:
  model_s **worldModelSlot = nullptr;
  cl_entity_s *worldEntity = nullptr;
  model_s *savedWorldModel = nullptr;
  model_s *savedEntityModel = nullptr;
  cvar_t *noVisCvar = nullptr;
  float savedNoVis = 0.0f;
  bool installed = false;
};

// ------------------------------------------------------------ render loop -

bool shouldRenderViews() {
  // Only the engine's own per-frame call renders views (V_RenderView is
  // also callable by other plugins to refresh the refdef) and never during
  // demo playback (the recorded view overwrites the refdef afterwards) or
  // re-entrantly.
  if (!available() || !StudioInterface || !*StudioInterface ||
      PaintBackgroundDepth <= 0 || RenderingViews || views().empty())
    return false;
  if (gEngfuncs.pDemoAPI && gEngfuncs.pDemoAPI->IsPlayingback())
    return false;
  return Engine.clientState && Engine.clientSignon && Engine.worldModel &&
         Engine.worldEntity;
}

void renderStandaloneViews() {
  RenderingViews = true;
  {
    // Only an active client has a fully loaded world; check before forcing.
    const bool worldReady = *Engine.clientState == ClientStateActive &&
                            *Engine.clientSignon == SignonsComplete;
    ClientStateScope clientState(Engine.clientState, Engine.clientSignon);
    EmptyWorldScope emptyWorld(Engine.worldModel, Engine.worldEntity,
                               worldReady);
    // Callbacks cannot unregister views; views they register start with the
    // next frame (the loop walks the original list by index).
    const size_t count = views().size();
    for (size_t i = 0; i < count; i++) {
      View *view = views()[i];
      if (!view->isActive())
        continue;
      CurrentView = view;
      view->onBeforeRenderView();
      if (OriginalRenderView)
        OriginalRenderView();
      view->onAfterRenderView();
      CurrentView = nullptr;
    }
  }
  RenderingViews = false;
}

void __cdecl RenderViewHookFn() {
  if (shouldRenderViews())
    renderStandaloneViews();
  if (OriginalRenderView)
    OriginalRenderView();
}

void __cdecl PaintBackgroundHookFn(int *extents) {
  ++PaintBackgroundDepth;
  if (OriginalPaintBackground)
    OriginalPaintBackground(extents);
  --PaintBackgroundDepth;
}

// ------------------------------------------------------------------ hooks -

// Soft resolution: the issue comment's degradation rule - an unknown engine
// build disables the route instead of failing startup.
bool resolve(void *module, const char *name, mh_gamesymbol_kind_t kind,
             void **address) {
  if (!module)
    return false;
  const auto status =
      g_pMetaHookAPI->ResolveGameSymbol(module, name, kind, address);
  if (status == MH_GAMESYMBOL_OK && *address != nullptr)
    return true;
  abc::ConsoleWarn("[AbcUi] standalone view symbol unavailable: %s "
                   "(status %d)\n",
                   name, static_cast<int>(status));
  return false;
}

bool isSvenCoopClient() {
  const auto clientFactory = g_pMetaHookAPI->GetClientFactory();
  return clientFactory && clientFactory("SCClientDLL001", nullptr) != nullptr;
}

void uninstallPortalHooks() {
  if (ThirdPersonHook) {
    g_pMetaHookAPI->UnHook(ThirdPersonHook);
    ThirdPersonHook = nullptr;
  }
  if (ResetAllHook) {
    g_pMetaHookAPI->UnHook(ResetAllHook);
    ResetAllHook = nullptr;
  }
  if (RenderPortalsHook) {
    g_pMetaHookAPI->UnHook(RenderPortalsHook);
    RenderPortalsHook = nullptr;
  }
  OriginalResetAll = nullptr;
  OriginalThirdPerson = nullptr;
  OriginalRenderPortals = nullptr;
}

// SvEngine's V_RenderView lets the Sven client render its portals before
// every pass; a standalone pass must not re-render them, and without a map
// the client's portal state is invalid (main-menu crashes without these
// hooks: RenderPortals dereferences map state, ResetAll calls GL entry
// points the client initializes only on map load).
// The portal hook bodies (thiscall via __fastcall + dummy).
void __fastcall portalRenderPortalsHook(void *pthis, int,
                                        ref_params_s *pparams) {
  if (CurrentView)
    return;
  if (OriginalRenderPortals)
    OriginalRenderPortals(pthis, 0, pparams);
}

int __fastcall portalResetAllHook(void *pthis, int) {
  if (CurrentView)
    return 0;
  if (OriginalResetAll)
    return OriginalResetAll(pthis, 0);
  return 0;
}

int __cdecl thirdPersonHook() {
  // This is an external view, never the client's first-person body pass.
  // Otherwise Sven consumes its pending local-player draw here: there is no
  // local player at the menu, and in a level it belongs to the real view.
  if (CurrentView)
    return 1;
  return OriginalThirdPerson ? OriginalThirdPerson() : 0;
}

bool installPortalHooks() {
  void *renderPortals = nullptr;
  void *resetAll = nullptr;
  if (!resolve(abc::g_dwClientBase, "ClientPortalManager_RenderPortals",
               MH_GAMESYMBOL_KIND_FUNCTION, &renderPortals) ||
      !resolve(abc::g_dwClientBase, "ClientPortalManager_ResetAll",
               MH_GAMESYMBOL_KIND_FUNCTION, &resetAll))
    return false;
  const auto thirdPerson = GetProcAddress(
      static_cast<HMODULE>(abc::g_dwClientBase), "CL_IsThirdPerson");
  if (!thirdPerson)
    return false;
  RenderPortalsHook = g_pMetaHookAPI->InlineHook(
      renderPortals, reinterpret_cast<void *>(&portalRenderPortalsHook),
      reinterpret_cast<void **>(&OriginalRenderPortals));
  ResetAllHook = g_pMetaHookAPI->InlineHook(
      resetAll, reinterpret_cast<void *>(&portalResetAllHook),
      reinterpret_cast<void **>(&OriginalResetAll));
  ThirdPersonHook = g_pMetaHookAPI->InlineHook(
      reinterpret_cast<void *>(thirdPerson),
      reinterpret_cast<void *>(&thirdPersonHook),
      reinterpret_cast<void **>(&OriginalThirdPerson));
  if (!RenderPortalsHook || !ResetAllHook || !ThirdPersonHook) {
    uninstallPortalHooks();
    return false;
  }
  return true;
}

// ------------------------------------------------------------ drawfuncs ---

struct StudioLightBridge {
  hook_t *Hook = nullptr;
  void (*Target)(alight_t *) = nullptr;
  void (*Original)(alight_t *) = nullptr;
  const StudioLighting *Active = nullptr;
};

StudioLightBridge &studioLightBridge() {
  static StudioLightBridge bridge;
  return bridge;
}

void studioSetupLighting(alight_t *light) {
  auto &bridge = studioLightBridge();
  if (!bridge.Original)
    return;
  if (!bridge.Active || !light) {
    bridge.Original(light);
    return;
  }
  const auto &config = *bridge.Active;
  float direction[3];
  const float length = std::sqrt(config.Direction[0] * config.Direction[0] +
                                 config.Direction[1] * config.Direction[1] +
                                 config.Direction[2] * config.Direction[2]);
  for (int i = 0; i < 3; ++i)
    direction[i] =
        length > 0 ? config.Direction[i] / length : (i == 0 ? -1.0f : 0.0f);
  alight_t preview = *light;
  preview.ambientlight = config.Enabled ? config.Ambient : 255;
  preview.shadelight = config.Enabled ? config.Shade : 0;
  for (int i = 0; i < 3; ++i)
    preview.color[i] = config.Enabled ? config.Color[i] : 1.0f;
  preview.plightvec = direction;
  // Override at the final lighting seam, after dynamic/entity lighting has
  // run. StudioSetupLighting consumes these values synchronously.
  bridge.Original(&preview);
}

void installStudioLighting(engine_studio_api_s *api) {
  auto &bridge = studioLightBridge();
  bridge.Active = nullptr;
  // This API function belongs to the engine, not the reloadable client DLL.
  // Retain its hook across HUD cycles and client interface recaptures.
  if (!api || !api->StudioSetupLighting ||
      bridge.Target == api->StudioSetupLighting)
    return;
  if (bridge.Hook)
    g_pMetaHookAPI->UnHook(bridge.Hook);
  bridge = {};
  bridge.Target = api->StudioSetupLighting;
  // The client caches a COPY of the Studio API table. Hook the API function
  // itself rather than rewriting a table entry that the client no longer reads.
  bridge.Hook =
      g_pMetaHookAPI->InlineHook(reinterpret_cast<void *>(bridge.Target),
                                 reinterpret_cast<void *>(&studioSetupLighting),
                                 reinterpret_cast<void **>(&bridge.Original));
  if (!bridge.Hook) {
    bridge.Target = nullptr;
    abc::ConsoleWarn("[AbcUi model] custom Studio lighting unavailable; "
                     "using engine lighting\n");
  }
}

class StudioLightingScope {
public:
  explicit StudioLightingScope(const StudioLighting *lighting)
      : Saved(studioLightBridge().Active) {
    studioLightBridge().Active = lighting;
  }
  ~StudioLightingScope() {
    studioLightBridge().Active = Saved;
  }
  StudioLightingScope(const StudioLightingScope &) = delete;
  StudioLightingScope &operator=(const StudioLightingScope &) = delete;

private:
  const StudioLighting *Saved;
};

// Sets currententity and r_blend the way the engine's entity lists do.
class EntityDrawScope {
public:
  explicit EntityDrawScope(cl_entity_s *ent)
      : savedEntity(*Engine.currententity), savedBlend(*Engine.r_blend) {
    *Engine.currententity = ent;
    *Engine.r_blend = ent->curstate.rendermode == kRenderNormal
                          ? 1.0f
                          : Engine.CL_FxBlend(ent) / 255.0f;
  }
  ~EntityDrawScope() {
    *Engine.currententity = savedEntity;
    *Engine.r_blend = savedBlend;
  }
  EntityDrawScope(const EntityDrawScope &) = delete;
  EntityDrawScope &operator=(const EntityDrawScope &) = delete;

  bool visible() const {
    return *Engine.r_blend > 0.0f;
  }

private:
  cl_entity_s *savedEntity;
  float savedBlend;
};

bool studioModelPass(cl_entity_s *ent, cl_entity_s *boneSource,
                     const StudioLighting *lighting, int flags) {
  if (!ent || !ent->model || !StudioInterface || !*StudioInterface ||
      !(*StudioInterface)->StudioDrawModel)
    return false;
  if (boneSource) {
    if (!boneSource->model)
      return false;
    EntityDrawScope sourceScope(boneSource);
    // StudioMergeBones consumes the LAST StudioSaveBones result, not aiment.
    // Re-prime it for each follower: drawing one follower replaces that cache.
    // flags=0 also bypasses bbox culling for animation-only source models.
    if (!(*StudioInterface)->StudioDrawModel(0))
      return false;
  }
  EntityDrawScope scope(ent);
  if ((flags & STUDIO_RENDER) && !scope.visible())
    return false;
  StudioLightingScope lightingScope(lighting);
  // Only STUDIO_RENDER or 0, never STUDIO_EVENTS: a preview must not fire
  // sounds, muzzle flashes or attachment writes.
  return (*StudioInterface)->StudioDrawModel(flags) != 0;
}

bool StudioDrawfuncs::setupStudioPose(cl_entity_s *ent,
                                      cl_entity_s *boneSource) {
  return studioModelPass(ent, boneSource, nullptr, 0);
}

void StudioDrawfuncs::drawStudioModel(cl_entity_s *ent, cl_entity_s *boneSource,
                                      const StudioLighting *lighting) {
  studioModelPass(ent, boneSource, lighting, STUDIO_RENDER);
}

} // namespace

// ---------------------------------------------------------------- public --

void registerView(View *view) {
  if (!view)
    return;
  for (const View *existing : views())
    if (existing == view)
      return;
  views().push_back(view);
}

void unregisterView(View *view) {
  // The list is in use until the views have rendered; unregistering from a
  // callback is a contract violation - keep the view (it is about to be
  // destroyed anyway only outside callbacks, per the interface contract).
  if (RenderingViews) {
    abc::ConsoleWarn("[AbcUi] standalone view unregistered from inside its "
                     "own callback; ignored\n");
    return;
  }
  std::erase(views(), view);
}

bool available() {
  return Available && ClientHooksReady;
}

void installEngineHooks() {
  if (EngineHooksInstalled)
    return;
  EngineHooksInstalled = true;
  Available = false;

  if (!g_pMetaHookAPI || !abc::g_dwEngineBase) {
    abc::ConsoleWarn("[AbcUi] standalone view route unavailable (engine "
                     "module base missing); model preview disabled\n");
    return;
  }

  void *renderView = nullptr;
  void *paintBackground = nullptr;
  EngineBridge engine{};
  if (!resolve(abc::g_dwEngineBase, "V_RenderView", MH_GAMESYMBOL_KIND_FUNCTION,
               &renderView) ||
      !resolve(abc::g_dwEngineBase, "VGui_ViewportPaintBackground",
               MH_GAMESYMBOL_KIND_FUNCTION, &paintBackground) ||
      !resolve(abc::g_dwEngineBase, "cls.state", MH_GAMESYMBOL_KIND_GLOBAL,
               reinterpret_cast<void **>(&engine.clientState)) ||
      !resolve(abc::g_dwEngineBase, "cls.signon", MH_GAMESYMBOL_KIND_GLOBAL,
               reinterpret_cast<void **>(&engine.clientSignon)) ||
      !resolve(abc::g_dwEngineBase, "cl_worldmodel", MH_GAMESYMBOL_KIND_GLOBAL,
               reinterpret_cast<void **>(&engine.worldModel)) ||
      !resolve(abc::g_dwEngineBase, "r_worldentity", MH_GAMESYMBOL_KIND_GLOBAL,
               reinterpret_cast<void **>(&engine.worldEntity)) ||
      !resolve(abc::g_dwEngineBase, "CL_FxBlend", MH_GAMESYMBOL_KIND_FUNCTION,
               reinterpret_cast<void **>(&engine.CL_FxBlend)) ||
      !resolve(abc::g_dwEngineBase, "currententity", MH_GAMESYMBOL_KIND_GLOBAL,
               reinterpret_cast<void **>(&engine.currententity)) ||
      !resolve(abc::g_dwEngineBase, "r_blend", MH_GAMESYMBOL_KIND_GLOBAL,
               reinterpret_cast<void **>(&engine.r_blend)) ||
      !renderView || !paintBackground) {
    abc::ConsoleWarn("[AbcUi] standalone view route unavailable (engine "
                     "gamedata symbols unresolved on this build); model "
                     "preview disabled\n");
    return;
  }

  RenderViewHook = g_pMetaHookAPI->InlineHook(
      renderView, reinterpret_cast<void *>(&RenderViewHookFn),
      reinterpret_cast<void **>(&OriginalRenderView));
  PaintBackgroundHook = g_pMetaHookAPI->InlineHook(
      paintBackground, reinterpret_cast<void *>(&PaintBackgroundHookFn),
      reinterpret_cast<void **>(&OriginalPaintBackground));
  if (!RenderViewHook || !PaintBackgroundHook) {
    if (PaintBackgroundHook) {
      g_pMetaHookAPI->UnHook(PaintBackgroundHook);
      PaintBackgroundHook = nullptr;
    }
    if (RenderViewHook) {
      g_pMetaHookAPI->UnHook(RenderViewHook);
      RenderViewHook = nullptr;
    }
    abc::ConsoleWarn("[AbcUi] standalone view route unavailable (engine "
                     "hook install failed); model preview disabled\n");
    return;
  }

  Engine = engine;
  Available = true;
}

void installClientHooks() {
  if (!Available || ClientHooksReady)
    return;
  // The portal hooks are required on a Sven client (see installPortalHooks)
  // and the whole route degrades without them.
  if (isSvenCoopClient() && !installPortalHooks()) {
    abc::ConsoleWarn("[AbcUi] standalone view route unavailable (client "
                     "portal gamedata symbols unresolved); model preview "
                     "disabled\n");
    return;
  }
  ClientHooksReady = true;
}

void uninstallClientHooks() {
  // HUD_Shutdown also runs without unloading the client. Keep its studio
  // slot/API until the next real LoadClient; HUD_Init re-arms portal hooks.
  ClientHooksReady = false;
  uninstallPortalHooks();
}

bool hookCalcRefdef(ref_params_s *pparams) {
  if (!CurrentView)
    return false;
  // The client's V_CalcRefdef is skipped: its bob, punch and pitch drift
  // state advances per call and belongs to the main view only.
  CurrentView->calcRefdef(pparams);
  pparams->onlyClientDraw = 1;
  pparams->nextView = 0;
  return true;
}

bool hookDrawNormalTriangles() {
  if (!CurrentView)
    return false;
  DrawingViewTriangles = true;
  CurrentView->drawNormalTriangles(&DrawfuncsInstance);
  DrawingViewTriangles = false;
  return true;
}

bool hookDrawTransparentTriangles() {
  if (!CurrentView)
    return false;
  DrawingViewTriangles = true;
  CurrentView->drawTransparentTriangles(&DrawfuncsInstance);
  DrawingViewTriangles = false;
  return true;
}

void captureStudioInterfaces(r_studio_interface_s **ppinterface,
                             engine_studio_api_s *pstudio) {
  // Keep the engine-owned slot, not its current value: when the client
  // declines the interface, the engine installs its fallback after return.
  StudioInterface = ppinterface;
  EngineStudio = pstudio;
  installStudioLighting(pstudio);
}

} // namespace standaloneview
} // namespace framework
