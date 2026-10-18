// panels::Radar implementation - see RadarPanel.h for the route-2 design.
// from: ABCEnchance/src/hud/vgui/radar.cpp (GBK->UTF-8).
#include <metahook.h>

#include "RadarPanel.h"
#include "../../framework/PanelManager.h"
#include <unordered_set>

#include <RmlUi/Core/Element.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <cstdio>
#include <string>

#include <com_model.h>

#include <abc/CvarCmd.h>
#include <abc/Math.h>

#include "../../render/Render.h"
#include "../../render/RenderCommon.h"
#include "../../rmlui/texture/Avatars.h"
#include "../../data/HudState.h"
#include "../../data/PlayerResource.h"
#include "../../framework/ImageLoader.h"
#include "../../framework/Theme.h"
#include "../../rmlui/AbcRenderInterface.h"
#include "../../rmlui/RmlUiModule.h"
#include "../../rmlui/VGUI2ExtensionImport.h"

// Windows.h min/max macros fight std::max (old repo precedent).
#undef max
#undef min

namespace panels {

namespace radar {

// Old ctor defaults (persisted contract - never rename).
cvar_t *Radar = nullptr;           // hud_radar (1 base, 2 round, 0 off)
cvar_t *RadarZoom = nullptr;       // hud_radar_zoom
cvar_t *RadarAvatar = nullptr;     // hud_radar_avatar
cvar_t *RadarAvatarSize = nullptr; // hud_radar_avatarsize
namespace {
RadarPanel *instance() {
  return static_cast<RadarPanel *>(framework::PanelManager::find("radar"));
}
} // namespace

void registerCvars() {
  Radar = abc::CREATE_CVAR("hud_radar", "1", abc::FCVAR_VALUE, nullptr);
  // Old callback -> ShowPanel(cvar->value): the panel re-evaluates the
  // value every frame (D-19: 0 hides the plate immediately).
  RadarZoom =
      abc::CREATE_CVAR("hud_radar_zoom", "2.5", abc::FCVAR_VALUE, nullptr);
  RadarAvatar =
      abc::CREATE_CVAR("hud_radar_avatar", "1", abc::FCVAR_VALUE, nullptr);
  RadarAvatarSize =
      abc::CREATE_CVAR("hud_radar_avatarsize", "20", abc::FCVAR_VALUE, nullptr);
}

namespace {

// Old ctor camera/FBO constants (RenderRadar).
constexpr int kFboSize = 1024;
constexpr float kCameraHeight = 72.0f;
constexpr float kOrthoBase = 4096.0f;
// Old m_vClearColor (Radar.ClearColor 0 0 0 255).
constexpr float kClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

FBO_Container_t RadarFbo = {};
bool FboReady = false;

// The map URI generation: bumped whenever the registered handle changes
// (registration time + frame-buffer regeneration). The Rml texture database
// caches per source string, so the <img> re-resolves through a new suffix.
int MapGeneration = 0;

// Square/round mask texture ids (old m_iRadarMaskId/m_iRadarRoundMaskId),
// uploaded through the VGUI surface channel; MaskSelect is the live slot the
// render interface reads at draw time (D-19: shape toggles need no reload).
int MaskSquare = 0;
int MaskRound = 0;
int MaskSelect = -1;

// Map <img> source with its generation suffix (see MapGeneration).
std::string MapUri;

void loadMaskTextures() {
  if (MaskSquare || MaskRound || !vgui2ext::available())
    return;
  const auto upload = [](const char *path) -> int {
    framework::ImageLoader::DecodedImage image;
    if (!framework::ImageLoader::loadFromFile(path, image))
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
// render interface; the panel's URI refresh picks the handle up.
void registerExternal() {
  rmlui::AbcRenderInterface *renderInterface = rmlui::renderInterface();
  if (!renderInterface || !FboReady)
    return;
  renderInterface->registerExternalTexture(
      "radarfbo://", static_cast<Rml::TextureHandle>(RadarFbo.s_hBackBufferTex),
      RadarFbo.iWidth, RadarFbo.iHeight, &MaskSelect);
}

// Old CRadarPanel::RenderRadar: render the world from +72u straight down
// into the radar FBO (world class only, entities/viewmodel/dynamics ducked).
void renderRadar() {
  IMetaRenderer *renderer = abc::MetaRenderer();
  if (!renderer || !FboReady)
    return;
  // Old cvar gate; D-19 adds the suit gate (without a suit the plate never
  // shows, so the full world pass would be invisible cost).
  if (!Radar || Radar->value <= 0 || !hudstate::hasSuit())
    return;
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
  // Old SetupOrthoProjectionMatrix(-worldSize..+worldSize, 2048, -2048)
  // with worldSize = 4096/zoom: the frustum half-extent (the visible span is
  // 2*worldSize - the same span the avatar math below divides by).
  const float zoom =
      RadarZoom && RadarZoom->value > 0.0f ? RadarZoom->value : 2.5f;
  const float worldHalf = kOrthoBase / zoom;
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
bool CallbacksRegistered = false;

} // namespace

void registerCommands() {
  // Old ADD_COMMAND pairs (HUD_Init time only - 08 §4.9).
  abc::ADD_COMMAND("+scale_radar", []() {
    if (RadarPanel *panel = instance())
      panel->setScale(true);
  });
  abc::ADD_COMMAND("-scale_radar", []() {
    if (RadarPanel *panel = instance())
      panel->setScale(false);
  });
}

// HUD_Frame-time assembly seam (called from onDocumentReady).
void startProducer() {
  if (CallbacksRegistered)
    return;
  CallbacksRegistered = true;
  if (IMetaRenderer *renderer = abc::MetaRenderer())
    renderer->RegisterRenderCallbacks(&Callbacks);
  loadMaskTextures();
  createFbo();
  registerExternal();
}

void stopProducer() {
  if (CallbacksRegistered) {
    CallbacksRegistered = false;
    if (IMetaRenderer *renderer = abc::MetaRenderer())
      renderer->UnregisterRenderCallbacks(&Callbacks);
  }
  if (rmlui::AbcRenderInterface *renderInterface = rmlui::renderInterface())
    renderInterface->unregisterExternalTexture("radarfbo://");
  if (MaskSquare) {
    vgui2ext::deleteTextureById(MaskSquare);
    MaskSquare = 0;
  }
  if (MaskRound) {
    vgui2ext::deleteTextureById(MaskRound);
    MaskRound = 0;
  }
  destroyFbo();
}

#ifndef NDEBUG
int debugFboTexture() {
  return FboReady ? static_cast<int>(RadarFbo.s_hBackBufferTex) : 0;
}
#endif

} // namespace radar

// Panel methods (radar namespace helpers above are TU-local via the unnamed
// namespace; the panel drives them through the exported seams).
// z-index -1: the radar plate occupies the top-left corner that the weapon
// chooser's slot row also uses; parking the radar under every default HUD
// document keeps the chooser (and the scoreboard) readable.
RadarPanel::RadarPanel()
    : HudPanel("radar", "abc/abcui/hud/rml/radar.rml", -1) {}

RadarPanel::~RadarPanel() {
  radar::stopProducer();
}

void RadarPanel::onDocumentReady() {
  radar::startProducer();
}

void RadarPanel::reset() {
  AppliedDom.clear();
  // Old CRadarPanel::Reset: SetScale(false).
  setScale(false);
}

void RadarPanel::setScale(bool state) {
  Scaled = state;
  if (!Document)
    return;
  // Old SetSize(m_iScaledWidth, m_iScaledTall) vs the res base box: the
  // base/scaled pair are the res design values 100 -> 300 (scaled_wide/tall
  // ride GetProportionalScaledValue, i.e. dp here).
  Rml::Element *plate = Document->GetElementById("plate");
  if (!plate)
    return;
  const char *size = state ? "300dp" : "100dp";
  plate->SetProperty("width", size);
  plate->SetProperty("height", size);
}

void RadarPanel::onData(double frameTime) {
  (void)frameTime;
  if (!Document)
    return;

  // Old Paint gate: cvar > 0 and a suit. D-19: hide (the old early return
  // froze the last plate) and also gate on the hide-all token.
  const bool enabled = radar::Radar && radar::Radar->value > 0;
  if (!enabled || !hudstate::hasSuit() ||
      hudstate::isHudHidden(hudstate::kHideAll)) {
    hide();
    return;
  }
  show();

  Rml::Element *square = Document->GetElementById("plate-square");
  Rml::Element *round = Document->GetElementById("plate-round");
  Rml::Element *map = Document->GetElementById("map");
  Rml::Element *north = Document->GetElementById("north");
  Rml::Element *avatars = Document->GetElementById("avatars");
  if (!square || !round || !map || !north || !avatars)
    return;

  // Old Paint: hud_radar 1 = square plate art, > 1 = round plate art; the
  // map mask follows (old m_pRadarImage->SetMaskTexture).
  const bool roundPlate = radar::Radar->value > 1.0f;
  square->SetProperty("display", roundPlate ? "none" : "block");
  round->SetProperty("display", roundPlate ? "block" : "none");
  radar::MaskSelect = roundPlate ? radar::MaskRound : radar::MaskSquare;

  // Map <img>: the external FBO texture through the registered URI; the
  // generation suffix re-resolves after frame-buffer regeneration.
  if (radar::FboReady) {
    const std::string uri =
        std::format("radarfbo://map?gen={}", radar::MapGeneration);
    if (radar::MapUri != uri) {
      radar::MapUri = uri;
      map->SetAttribute("src", Rml::String(uri));
    }
    map->SetProperty("display", "block");
  } else {
    map->SetProperty("display", "none");
  }

  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (!local || local->index <= 0)
    return;

  // Live plate size in px (the dp box x density): the old code's north/
  // avatar placement mixed scaled panel geometry with raw px offsets -
  // carried verbatim (see the header note).
  const float density = framework::Theme::scale();
  const float plateSize = (Scaled ? 300.0f : 100.0f) * density;
  const float center = plateSize / 2.0f;

  // Old north indicator: rides the plate rim, angle = local entity yaw
  // (curstate.angles, not view angles).
  {
    const float northSize = 5.0f * density; // old Northground 5 design units
    const float len = plateSize - northSize;
    const float rotate =
        abc::math::DegToRad(local->curstate.angles[abc::math::kYaw]);
    const float hh =
        roundPlate ? (len / 2.0f)
                   : static_cast<float>(
                         std::sqrt(2.0 * static_cast<double>(len) * len) / 2.0);
    const float stx = std::clamp(center + hh * cosf(rotate), 0.0f, len);
    const float sty = std::clamp(center + hh * sinf(rotate), 0.0f, len);
    north->SetProperty("left", std::to_string(stx) + "px");
    north->SetProperty("top", std::to_string(sty) + "px");
  }

  std::unordered_set<int> used;
  const playerres::PlayerInfo *localInfo = playerres::getLocalPlayerInfo();
  if (!radar::RadarAvatar || radar::RadarAvatar->value <= 0 || !localInfo) {
    for (auto [id, img] : AvatarElements)
      AppliedDom.property(img, "display", "none");
    return;
  }

  const float avatarSize =
      radar::RadarAvatarSize ? radar::RadarAvatarSize->value : 20.0f;
  const float avatarHalf = avatarSize / 2.0f;
  const float zoom = radar::RadarZoom && radar::RadarZoom->value > 0.0f
                         ? radar::RadarZoom->value
                         : 2.5f;
  // Old comment: "this should match the radar rendering scale" - the FBO
  // ortho spans +/-worldHalf, so the plate scale is plateSize / full span.
  const float worldHalf = radar::kOrthoBase / zoom;
  const float radarScale = plateSize / (2.0f * worldHalf);
  const float localYaw =
      abc::math::DegToRad(-local->curstate.angles[abc::math::kYaw]);
  const float cosYaw = cosf(localYaw);
  const float sinYaw = sinf(localYaw);

  for (int index = 1; index <= playerres::kMaxPlayers; index++) {
    const playerres::PlayerInfo *info = playerres::getPlayerInfo(index);
    if (!info || !info->isValid() || info->spectate ||
        info->team != localInfo->team)
      continue;
    const cl_entity_t *entity = gEngfuncs.GetEntityByIndex(index);
    if (!entity || entity->curstate.messagenum != local->curstate.messagenum ||
        !entity->player || !entity->model || local == entity)
      continue;

    const float relX = entity->curstate.origin[0] - local->curstate.origin[0];
    const float relY = entity->curstate.origin[1] - local->curstate.origin[1];
    const float radarX = -relY;
    const float radarY = relX;
    const float rotatedX = radarX * cosYaw - radarY * sinYaw;
    const float rotatedY = radarX * sinYaw + radarY * cosYaw;
    float screenX = center + rotatedX * radarScale - avatarHalf;
    float screenY = center - rotatedY * radarScale - avatarHalf;
    const float maxPos = std::max(0.0f, plateSize - avatarSize);
    screenX = std::clamp(screenX, 0.0f, maxPos);
    screenY = std::clamp(screenY, 0.0f, maxPos);
    if (roundPlate) {
      const float dx = screenX + avatarHalf - center;
      const float dy = screenY + avatarHalf - center;
      const float distance = sqrtf(dx * dx + dy * dy);
      const float maxRadius = center - avatarHalf;
      if (distance > maxRadius && distance > 0.0f) {
        const float ratio = maxRadius / distance;
        screenX = center + dx * ratio - avatarHalf;
        screenY = center + dy * ratio - avatarHalf;
      }
    }

    // Old SetPlayer(i+1, 32x32): the avatar texture resolves through
    // data::avatars (steamapi facade) with the cache-bust generation.
    const std::string uri = std::format("avatar://{}?gen={}", info->steamId64,
                                        avatars::generation(info->steamId64));
    used.insert(index);
    Rml::Element *&img = AvatarElements[index];
    if (!img) {
      auto holder = Document->CreateElement("img");
      if (!holder) {
        AvatarElements.erase(index);
        continue;
      }
      img = holder.get();
      img->SetClass("avatar", true);
      avatars->AppendChild(std::move(holder));
    }
    AppliedDom.property(img, "display", "block");
    AppliedDom.attribute(img, "src", uri);
    AppliedDom.property(img, "left", std::to_string(screenX) + "px");
    AppliedDom.property(img, "top", std::to_string(screenY) + "px");
    AppliedDom.property(img, "width", std::to_string(avatarSize) + "px");
    AppliedDom.property(img, "height", std::to_string(avatarSize) + "px");
  }
  for (auto it = AvatarElements.begin(); it != AvatarElements.end();) {
    if (!used.count(it->first)) {
      AppliedDom.forget(it->second);
      avatars->RemoveChild(it->second);
      it = AvatarElements.erase(it);
    } else
      ++it;
  }
}

} // namespace panels
