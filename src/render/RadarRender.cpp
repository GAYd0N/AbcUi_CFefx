// framework::radarrender implementation - see RadarRender.h. Ported from the
// built-in radar panel's render half (old CRadarPanel::RenderRadar, from
// ABCEnchance src/hud/vgui/radar.cpp). Since issue #24 the GPU half lives in
// the render backend (IWorldPass); this file keeps the orchestration: cvar
// save/restore, camera math, zoom, the mask slot and the external-texture
// registration.
#include "RadarRender.h"

#include <cstdint>
#include <string>

#include <metahook.h>

#include <com_model.h>

#include <RmlUi/Core/Types.h>

#include <abc/CvarCmd.h>
#include <abc/Math.h>

#include "../data/HudState.h"
#include "../rmlui/AbcRenderInterface.h"
#include "../rmlui/RmlUiModule.h"
#include "BackendHost.h"
#include "IRenderBackend.h"

namespace framework {
namespace radarrender {
namespace {

// Old ctor camera/FBO constants (RenderRadar).
constexpr int kFboSize = 1024;
constexpr float kCameraHeight = 72.0f;
constexpr float kOrthoBase = 4096.0f;
// Old m_vClearColor (Radar.ClearColor 0 0 0 255).
constexpr float kClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

bool Active = false;
float Zoom = 2.5f;

// The module uploads and selects its own mask texture.
int MaskSelect = 0;

// The backend owns the render target; the framework observes the texture id
// (it changes on creation / frame-buffer regeneration) and re-registers the
// external texture + bumps the generation so the module's <img> re-resolves.
unsigned int RegisteredTexture = 0;
int MapGeneration = 0;

void syncRegistration() {
  abc::render::IWorldPass *world = abc::render::worldPass();
  rmlui::AbcRenderInterface *renderInterface = rmlui::renderInterface();
  if (!world || !renderInterface)
    return;
  const unsigned int texture = world->texture();
  if (texture == 0) {
    if (RegisteredTexture != 0) {
      RegisteredTexture = 0;
      renderInterface->unregisterExternalTexture("radarfbo://");
    }
    return;
  }
  if (texture == RegisteredTexture)
    return;
  RegisteredTexture = texture;
  MapGeneration++;
  renderInterface->registerExternalTexture(
      "radarfbo://", static_cast<Rml::TextureHandle>(texture), kFboSize,
      kFboSize, &MaskSelect);
}

// Old CRadarPanel::RenderRadar framework half: render the world from +72u
// straight down into the backend's radar target (world class only,
// entities/viewmodel/dynamics ducked). Runs from the backend's world beat,
// inside its frame - the old RadarCallbacks::OnPreRenderView slot.
void renderRadar() {
  abc::render::IWorldPass *world = abc::render::worldPass();
  if (!world || !Active)
    return;
  // The suit gate lives with the module's visibility decision (it only calls
  // setActive while the plate is on screen), so the expensive world pass
  // never runs for an invisible plate.
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (!local || local->index <= 0)
    return;
  world->ensureTarget(kFboSize, kFboSize);
  syncRegistration();
  if (world->texture() == 0)
    return;

  // Old arySaveCvars duck-typing.
  cvar_t *drawEntities = abc::CVAR_GET_POINTER("r_drawentities");
  cvar_t *drawViewModel = abc::CVAR_GET_POINTER("r_drawviewmodel");
  cvar_t *drawDynamic = abc::CVAR_GET_POINTER("r_dynamic");
  cvar_t *water = abc::CVAR_GET_POINTER("r_water");
  cvar_t *shadow = abc::CVAR_GET_POINTER("r_shadow");
  cvar_t *deferred = abc::CVAR_GET_POINTER("r_deferred_lighting");
  const float savedEntities = drawEntities ? drawEntities->value : 0.0f;
  const float savedViewModel = drawViewModel ? drawViewModel->value : 0.0f;
  const float savedDynamic = drawDynamic ? drawDynamic->value : 0.0f;
  const float savedWater = water ? water->value : 0.0f;
  const float savedShadow = shadow ? shadow->value : 0.0f;
  const float savedDeferred = deferred ? deferred->value : 0.0f;
  if (drawEntities)
    drawEntities->value = 0;
  if (drawViewModel)
    drawViewModel->value = 0;
  if (drawDynamic)
    drawDynamic->value = 0;
  if (water)
    water->value = 0;
  if (shadow)
    shadow->value = 0;
  if (deferred)
    deferred->value = 0;

  abc::render::IWorldPass::Request request;
  request.width = kFboSize;
  request.height = kFboSize;
  for (int i = 0; i < 3; i++)
    request.origin[i] = local->curstate.origin[i];
  request.origin[2] += kCameraHeight;
  for (int i = 0; i < 3; i++)
    request.angles[i] = local->curstate.angles[i];
  request.angles[0] = 90.0f; // straight down
  request.angles[2] = 0.0f;  // no roll
  // Old SetupOrthoProjectionMatrix(-worldSize..+worldSize, 2048, -2048) with
  // worldSize = 4096/zoom: the frustum half-extent (the visible span is
  // 2*worldSize - the same span the module's avatar math divides by).
  request.orthoHalfExtent = kOrthoBase / Zoom;
  request.orthoNear = kOrthoBase;
  request.orthoFar = -kOrthoBase;
  for (int i = 0; i < 4; i++)
    request.clearColor[i] = kClearColor[i];
  world->render(request);

  if (drawEntities)
    drawEntities->value = savedEntities;
  if (drawViewModel)
    drawViewModel->value = savedViewModel;
  if (drawDynamic)
    drawDynamic->value = savedDynamic;
  if (water)
    water->value = savedWater;
  if (shadow)
    shadow->value = savedShadow;
  if (deferred)
    deferred->value = savedDeferred;
}

} // namespace

void onWorldPassBeat() {
  renderRadar();
}

void setActive(bool active) {
  Active = active;
  // Ensure the target exists before the first beat (the module may query
  // ready()/generation() right after presenting the plate).
  if (abc::render::IWorldPass *world = abc::render::worldPass()) {
    world->ensureTarget(kFboSize, kFboSize);
    syncRegistration();
  }
}

void setMask(int textureId) {
  MaskSelect = textureId;
}

void forgetMask(int textureId) {
  if (MaskSelect == textureId)
    MaskSelect = 0;
}

void setZoom(float zoom) {
  Zoom = zoom > 0.0f ? zoom : 2.5f;
}

bool ready() {
  const abc::render::IWorldPass *world = abc::render::worldPass();
  return world && world->texture() != 0;
}

int generation() {
  syncRegistration();
  return MapGeneration;
}

} // namespace radarrender
} // namespace framework
