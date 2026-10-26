#pragma once

// framework::standaloneview - the in-house "extra view" route (issue #24
// comment): render isolated 3D views (the model preview) through the
// ENGINE's own V_RenderView, with or without Renderer, instead of
// Renderer's IEntityViewRenderer isolated scope.
//
// The route is copied from MetaHookSv/StandaloneView (MIT, (c) 2021 hzqst):
// hook V_RenderView + VGui_ViewportPaintBackground, force cls.state /
// cls.signon active for the pass, install an empty brush worldmodel when no
// map is loaded, route V_CalcRefdef and the triangle callbacks to the
// current view with onlyClientDraw, and skip the Sven client's portal
// manager during the pass. Views render BEFORE the engine's main view.
//
// Degradation (this repo's rule, softer than StandaloneView's SysError):
// the engine/client symbols come from gamedata, and an unresolvable set on
// an unknown build disables the whole route - consumers fall into their
// existing "preview disabled" branches - instead of failing startup.

struct ref_params_s;
struct cl_entity_s;
struct r_studio_interface_s;
struct engine_studio_api_s;

#include <array>

namespace framework {
namespace standaloneview {

// Studio light-ray direction in world coordinates (opposite the direction
// from the model towards the light). Applied only during the requested draw.
struct StudioLighting {
  int Ambient = 96;
  int Shade = 96;
  std::array<float, 3> Color{1, 1, 1};
  std::array<float, 3> Direction{-1, 0, 0};
  bool Enabled = true;
};

// Entity draws handed to a view's triangle callbacks; valid only inside
// them. Only the studio draw exists (the preview's sole need); brush and
// sprite draws stay unported.
class Drawfuncs {
public:
  virtual ~Drawfuncs() = default;
  // Evaluate/save the merged pose without culling, meshes or events. The
  // engine Studio bone transforms remain readable until the next Studio call.
  virtual bool setupStudioPose(cl_entity_s *ent,
                               cl_entity_s *boneSource = nullptr) = 0;
  // StudioDrawModel/StudioDrawPlayer with STUDIO_RENDER and no STUDIO_EVENTS
  // (a preview must not fire model events), currententity and r_blend set
  // and restored the way the engine's entity lists do. For a private
  // MOVETYPE_FOLLOW entity, pass its bone source explicitly: these entities
  // are not registered in the engine's entity array. The source is evaluated
  // with flags=0 immediately before the draw (bones only, no mesh/events).
  virtual void drawStudioModel(cl_entity_s *ent,
                               cl_entity_s *boneSource = nullptr,
                               const StudioLighting *lighting = nullptr) = 0;
};

// A view rendered before the engine's main view, once per frame. Every
// callback runs on the main thread inside V_RenderView while cls.state /
// cls.signon are forced active; until a map is fully loaded the worldmodel
// is an empty brush model owned by this module.
class View {
public:
  virtual ~View() = default;
  // False skips the view this frame.
  virtual bool isActive() const = 0;
  // Fill vieworg, viewangles and viewport (x, y, width, height in window
  // pixels, top-left origin); leave the cl_* fields alone (the engine
  // writes them back). onlyClientDraw/nextView are forced after the call.
  virtual void calcRefdef(struct ref_params_s *pparams) = 0;
  virtual void onBeforeRenderView() = 0;
  // The view has been drawn into its viewport of the currently bound
  // framebuffer; copy the result out here.
  virtual void onAfterRenderView() = 0;
  // Replaces the client's triangle exports while this view renders; the
  // engine's 3D matrices are already set up.
  virtual void drawNormalTriangles(Drawfuncs *drawfuncs) = 0;
  virtual void drawTransparentTriangles(Drawfuncs *drawfuncs) = 0;
};

// Registers the view (ignored twice); a view registered from a callback
// starts rendering next frame. Must NOT be called from a view callback.
void registerView(View *view);
void unregisterView(View *view);

// True when the route installed (engine hooks resolved +, on a Sven client,
// portal hooks resolved). Consumers degrade when false.
bool available();

// OnLoadClient: engine hooks once per process. Portal hooks also re-arm on
// HUD_Init after each HUD_Shutdown. Repeated installs are idempotent.
// Soft-degrading - see the header note.
void installEngineHooks();
void installClientHooks();
// HUD_Shutdown: remove portal hooks before a possible client unload, but
// retain the studio slot across ordinary HUD cycles. A real LoadClient
// clears that slot with captureStudioInterfaces(nullptr, nullptr).
void uninstallClientHooks();

// hooks.cpp export routing: consumed returns true when a view's pass owns
// the call and the client original must be skipped.
bool hookCalcRefdef(ref_params_s *pparams);
bool hookDrawNormalTriangles();
bool hookDrawTransparentTriangles();

// hooks.cpp H_HUD_GetStudioModelInterface: capture the studio interface the
// engine received (the client's, or the engine's own when the client
// provides none) and the engine studio API. The engine-owned slot is read
// at draw time because the engine may fill its fallback after this export.
void captureStudioInterfaces(r_studio_interface_s **ppinterface,
                             engine_studio_api_s *pstudio);

} // namespace standaloneview
} // namespace framework
