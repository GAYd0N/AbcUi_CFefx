// framework::radarrender implementation - see RadarRender.h. Ported from the
// built-in radar panel's render half (old CRadarPanel::RenderRadar, from
// ABCEnchance src/hud/vgui/radar.cpp).
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
#include "../rmlui/VGUI2ExtensionImport.h"
#include "../framework/ImageLoader.h"
#include "Render.h"
#include "RenderCommon.h"

namespace framework {
namespace radarrender {
namespace {

// Old ctor camera/FBO constants (RenderRadar).
constexpr int kFboSize = 1024;
constexpr float kCameraHeight = 72.0f;
constexpr float kOrthoBase = 4096.0f;
// Old m_vClearColor (Radar.ClearColor 0 0 0 255).
constexpr float kClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

FBO_Container_t RadarFbo = {};
bool FboReady = false;
bool Active = false;
bool Started = false;
float Zoom = 2.5f;

// Square/round mask texture ids (old m_iRadarMaskId/m_iRadarRoundMaskId),
// uploaded through the VGUI surface channel; MaskSelect is the live slot the
// render interface reads at draw time (D-19: shape toggles need no reload).
int MaskSquare = 0;
int MaskRound = 0;
int MaskSelect = -1;

// The map URI generation: bumped whenever the registered handle changes
// (registration time + frame-buffer regeneration).
int MapGeneration = 0;

void loadMaskTextures() {
  if (MaskSquare || MaskRound || !vgui2ext::available())
    return;
  const auto upload = [](const char *path) -> int {
    ImageLoader::DecodedImage image;
    if (!ImageLoader::loadFromFile(path, image))
      return 0;
    const int id = vgui2ext::createTexture();
    if (!id)
      return 0;
    vgui2ext::uploadTexture(id, image.Pixels.data(), image.Width, image.Height);
    return id;
  };
  MaskSquare = upload("abc/abcui/hud/img/radar/radar_mask.png");
  MaskRound = upload("abc/abcui/hud/img/radar/radar_roundmask.png");
}

void createFbo() {
  IMetaRenderer *renderer = abc::MetaRenderer();
  if (!renderer)
    return;
  if (FboReady)
    renderer->FreeFBO(&RadarFbo);
  RadarFbo = {};
  RadarFbo.iWidth = kFboSize;
  RadarFbo.iHeight = kFboSize;
  renderer->GenFrameBuffer(&RadarFbo, "RadarPanelFBO");
  renderer->FrameBufferColorTexture(&RadarFbo, GL_RGBA8);
  renderer->FrameBufferDepthTexture(&RadarFbo, GL_DEPTH24_STENCIL8);
  FboReady = RadarFbo.s_hBackBufferTex != 0;
  MapGeneration++;
}

void destroyFbo() {
  if (!FboReady)
    return;
  if (IMetaRenderer *renderer = abc::MetaRenderer())
    renderer->FreeFBO(&RadarFbo);
  RadarFbo = {};
  FboReady = false;
  // The registered handle died with the FBO; the next generate re-registers
  // (+ bumps MapGeneration so the <img> re-resolves).
  if (rmlui::AbcRenderInterface *renderInterface = rmlui::renderInterface())
    renderInterface->unregisterExternalTexture("radarfbo://");
}

// Pushes the live registration (FBO colour texture + mask slot) into the
// render interface; the module's URI refresh picks the handle up.
void registerExternal() {
  rmlui::AbcRenderInterface *renderInterface = rmlui::renderInterface();
  if (!renderInterface || !FboReady)
    return;
  renderInterface->registerExternalTexture(
      "radarfbo://", static_cast<Rml::TextureHandle>(RadarFbo.s_hBackBufferTex),
      RadarFbo.iWidth, RadarFbo.iHeight, &MaskSelect);
}

// Old CRadarPanel::RenderRadar: render the world from +72u straight down into
// the radar FBO (world class only, entities/viewmodel/dynamics ducked).
void renderRadar() {
  IMetaRenderer *renderer = abc::MetaRenderer();
  if (!renderer || !FboReady || !Active)
    return;
  // The suit gate lives with the module's visibility decision (it only calls
  // setActive while the plate is on screen), so the expensive world pass
  // never runs for an invisible plate.
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (!local || local->index <= 0)
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

  renderer->BeginDebugGroup("CRadarPanel::RenderRadar");
  renderer->BindFrameBuffer(&RadarFbo);
  renderer->SetCurrentSceneFBO(&RadarFbo);

  vec3_t origin{};
  vec3_t angles{};
  for (int i = 0; i < 3; i++)
    origin[i] = local->curstate.origin[i];
  origin[2] += kCameraHeight;
  for (int i = 0; i < 3; i++)
    angles[i] = local->curstate.angles[i];
  angles[0] = 90.0f; // straight down
  angles[2] = 0.0f;  // no roll

  renderer->PushRefDef();
  // Multiview on: R_SetupGL then leaves viewport/projection/world matrix to
  // us (the old code's own trick); the UBO upload below replaces
  // R_UploadCameraUBO, which self-skips under multiview.
  renderer->SetMultiviewEnabled(true);
  const int oldDrawClassify = renderer->GetDrawClassify();
  renderer->SetDrawClassify(DRAW_CLASSIFY_WORLD | DRAW_CLASSIFY_LIGHTMAP);
  renderer->SetRefDefViewOrigin(origin);
  renderer->SetRefDefViewAngles(angles);
  renderer->UpdateRefDef();
  renderer->LoadIdentityForProjectionMatrix();
  // Old SetupOrthoProjectionMatrix(-worldSize..+worldSize, 2048, -2048) with
  // worldSize = 4096/zoom: the frustum half-extent (the visible span is
  // 2*worldSize - the same span the module's avatar math divides by).
  const float worldHalf = kOrthoBase / Zoom;
  renderer->SetupOrthoProjectionMatrix(-worldHalf, worldHalf, -worldHalf,
                                       worldHalf, kOrthoBase, -kOrthoBase,
                                       true);
  renderer->LoadIdentityForWorldMatrix();
  renderer->SetupPlayerViewWorldMatrix(origin, angles);
  renderer->SetViewport(0, 0, kFboSize, kFboSize);

  camera_ubo_t cameraUbo = {};
  renderer->SetupCameraView(&cameraUbo.views[0]);
  cameraUbo.numViews = 1;
  renderer->UploadCameraUBOData(&cameraUbo);

  renderer->ClearColorDepthStencil(const_cast<float *>(kClearColor), 1.0f, 0,
                                   STENCIL_MASK_ALL);
  renderer->RenderScene();

  renderer->SetDrawClassify(oldDrawClassify);
  renderer->SetMultiviewEnabled(false);
  renderer->PopRefDef();
  renderer->EndDebugGroup();

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

// Engine-lifetime render callbacks (old CMetaRendererCallbacks radar half +
// the ModelViewPanel frame-buffer regeneration seam; the old radar lacked the
// latter - D-19 closes that latent hole).
class RadarCallbacks : public IMetaRendererCallbacks {
public:
  void OnBeginRendering(int *, int *, int *, int *) override {}
  void OnRenderFrameStart() override {}
  void OnBeginRenderingPost(int *, int *, int *, int *) override {}
  void OnFreeFrameBuffers() override {
    destroyFbo();
  }
  void OnGenerateFrameBuffers() override {
    createFbo();
    registerExternal();
  }
  void OnPreRenderView() override {
    renderRadar();
  }
  void OnPreRenderViewPost() override {}
  void OnPostRenderView() override {}
  void OnPostRenderViewPost() override {}
  void OnRenderEndFrame() override {}
};

RadarCallbacks Callbacks;

void startProducer() {
  if (Started)
    return;
  Started = true;
  if (IMetaRenderer *renderer = abc::MetaRenderer())
    renderer->RegisterRenderCallbacks(&Callbacks);
  loadMaskTextures();
  createFbo();
  registerExternal();
}

} // namespace

void setActive(bool active) {
  startProducer();
  Active = active;
}

void setMaskRound(bool round) {
  MaskSelect = round ? MaskRound : MaskSquare;
}

void setZoom(float zoom) {
  Zoom = zoom > 0.0f ? zoom : 2.5f;
}

bool ready() {
  return FboReady;
}

int generation() {
  return MapGeneration;
}

} // namespace radarrender
} // namespace framework
