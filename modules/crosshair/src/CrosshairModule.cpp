// abcui.crosshair - the dynamic crosshair and the per-weapon sprite
// crosshair. Moved from src/panels/crosshair (old CCrosshairPanel, ABCEnchance
// sources: src/hud/vgui/crosshair.*).
//
// The 5 vgui child panels (center/top/bottom/left/right) and their 5 outline
// siblings become 10 absolutely-positioned divs driven per frame in the
// redraw phase (old OnThink/PaintBackground); the CSPRImage default crosshair
// becomes one <img> whose src flips between the current weapon's
// spralpha:// crops (old kRenderTransTexture draw - NOT the additive flavor).
//
// Geometry is in raw screen pixels (px), exactly like the old SetBounds calls
// on the proportional vgui panel - the cl_crosshair* cvars are pixel values
// and do not ride the HUD dp scale. Port note kept from the built-in copy: the
// old drift preferred the EVVectorScale sig-scan capture (gHookFuncs client
// hook, AbcUi has no client sig infrastructure); the port always uses the old
// code's own fallback (clientdata punchangle).
#include <abcui/events.h>
#include "../../common/Text.h"
#include <abcui/module_api.h>

#include <cmath>
#include <cstdio>
#include <format>
#include <cstring>
#include <string>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};

constexpr const char *kPanelName = "crosshair";
constexpr const char *kDocumentPath =
    "mod://abcui.crosshair/hud/rml/crosshair.rml";

// Old WEAPONSTATE / ITEM_FLAG bits used here.
constexpr int32_t kStateOnTarget = 1 << 1;

constexpr const char *kTransparent = "rgba(0, 0, 0, 0)";

// Old CCrosshairPanel ctor defaults (persisted contract - never rename).
constexpr const char *kCrosshair = "cl_crosshair";
constexpr const char *kAutoHide = "cl_crosshairautohide";
constexpr const char *kSize = "cl_crosshairsize";
constexpr const char *kThickness = "cl_crosshairthickness";
constexpr const char *kGap = "cl_crosshairgap";
constexpr const char *kMultiple = "cl_crosshairmultiple";
constexpr const char *kAlpha = "cl_crosshairalpha";
constexpr const char *kColorR = "cl_crosshaircolor_r";
constexpr const char *kColorG = "cl_crosshaircolor_g";
constexpr const char *kColorB = "cl_crosshaircolor_b";
constexpr const char *kOutlineAlpha = "cl_crosshair_outline_alpha";
constexpr const char *kOutlineR = "cl_crosshaircolor_outline_r";
constexpr const char *kOutlineG = "cl_crosshaircolor_outline_g";
constexpr const char *kOutlineB = "cl_crosshaircolor_outline_b";
constexpr const char *kOutlineDraw = "cl_crosshair_outline_draw";
constexpr const char *kOutlineWidth = "cl_crosshair_outline";
constexpr const char *kTShape = "cl_crosshair_t";
constexpr const char *kDot = "cl_crosshairdot";
constexpr const char *kAnimInterval = "cl_crosshair_anim_interval";

AbcUiCvarHandle Crosshair = nullptr;
AbcUiCvarHandle AutoHide = nullptr;
AbcUiCvarHandle Size = nullptr;
AbcUiCvarHandle Thickness = nullptr;
AbcUiCvarHandle Gap = nullptr;
AbcUiCvarHandle Multiple = nullptr;
AbcUiCvarHandle Alpha = nullptr;
AbcUiCvarHandle ColorR = nullptr;
AbcUiCvarHandle ColorG = nullptr;
AbcUiCvarHandle ColorB = nullptr;
AbcUiCvarHandle OutlineAlpha = nullptr;
AbcUiCvarHandle OutlineR = nullptr;
AbcUiCvarHandle OutlineG = nullptr;
AbcUiCvarHandle OutlineB = nullptr;
AbcUiCvarHandle OutlineDraw = nullptr;
AbcUiCvarHandle OutlineWidth = nullptr;
AbcUiCvarHandle TShape = nullptr;
AbcUiCvarHandle Dot = nullptr;
AbcUiCvarHandle AnimInterval = nullptr;
// Old pCvarDefaultCrosshair / pCvarDefaultFOV: the vanilla "crosshair" toggle
// and the engine "default_fov" (zoom detection).
AbcUiCvarHandle VanillaCrosshair = nullptr;
AbcUiCvarHandle DefaultFov = nullptr;

struct AppliedPart {
  std::string color;
  float bounds[4] = {};
  bool valid = false;
};

struct Part {
  const char *core;
  const char *outline;
};

// DOM order: center, top, bottom, left, right (old m_aryCrosshair order).
const Part kParts[5] = {{"core-center", "outline-center"},
                        {"core-top", "outline-top"},
                        {"core-bottom", "outline-bottom"},
                        {"core-left", "outline-left"},
                        {"core-right", "outline-right"}};

float CenterX = 0.0f;
float CenterY = 0.0f;
float PreOffset = 0.0f;
double AnimTime = 0.0;
double ClientTimeNow = 0.0;
AppliedPart Core[5];
AppliedPart Outline[5];
int CurrentWeaponId = 0;
// Current spr uri (avoids re-assigning src every frame - texture churn).
std::string SprUri;

AbcUiDocument *panelDocument() {
  return Api->ui->panelDocument(kPanelName);
}

float cvarValue(AbcUiCvarHandle cvar, float fallback) {
  return cvar ? Api->console->cvarValue(cvar, fallback) : fallback;
}

int cvarInt(AbcUiCvarHandle cvar, int fallback) {
  return cvar ? static_cast<int>(
                    Api->console->cvarValue(cvar, static_cast<float>(fallback)))
              : fallback;
}

std::string pixelValue(float value) {
  return std::format("{:g}px", value);
}

// Writes one crosshair part when its target state differs from the applied
// one (property-churn guard).
void applyPart(AbcUiElement *element, AppliedPart &applied, const char *color,
               const float bounds[4]) {
  if (!element)
    return;
  if (applied.valid && applied.color == color &&
      memcmp(applied.bounds, bounds, sizeof(applied.bounds)) == 0)
    return;
  applied.valid = true;
  applied.color = color;
  memcpy(applied.bounds, bounds, sizeof(applied.bounds));
  const std::string left = pixelValue(bounds[0]);
  const std::string top = pixelValue(bounds[1]);
  const std::string width = pixelValue(bounds[2]);
  const std::string height = pixelValue(bounds[3]);
  Api->ui->elementSetProperty(element, "background-color", color);
  Api->ui->elementSetProperty(element, "left", left.c_str());
  Api->ui->elementSetProperty(element, "top", top.c_str());
  Api->ui->elementSetProperty(element, "width", width.c_str());
  Api->ui->elementSetProperty(element, "height", height.c_str());
}

AbcUiElement *elementOf(AbcUiDocument *doc, const char *id) {
  return doc ? Api->ui->querySelector(doc, id) : nullptr;
}

void setElementProperty(const char *id, const char *name, const char *value) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (AbcUiElement *element = elementOf(doc, id))
    Api->ui->elementSetProperty(element, name, value);
}

// Hides every dynamic part + the spr img (the gate paths where the old
// PaintBackground/OnThink left everything empty).
void hideDynamicCrosshair() {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  // Old HideDynamicCrossHair zeroed the ten panel colors; the spr img has no
  // old counterpart (vgui simply stopped painting it) - display:none is the
  // Rml equivalent of "not composited this frame".
  for (const Part &part : kParts) {
    setElementProperty(part.core, "background-color", kTransparent);
    setElementProperty(part.outline, "background-color", kTransparent);
  }
  for (auto &part : Core)
    part.valid = false;
  for (auto &part : Outline)
    part.valid = false;
  setElementProperty("spr-crosshair", "display", "none");
  SprUri.clear();
}

void updateCenter() {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (Api->data->thirdPerson()) {
    // Old read: GetPlayerTrace()->Get(VIEW)->endpos - the module is pumped in
    // HUD_Frame, this consumer runs in the render phase (the old OnThink
    // phase), same ordering as the old code.
    float endPos[3] = {};
    float screen[2] = {};
    if (Api->data->traceEndPos(ABCUI_TRACE_VIEW, endPos))
      Api->ui->project(endPos, screen);
    CenterX = screen[0];
    CenterY = screen[1];
  } else {
    int width = 0;
    int height = 0;
    if (Api->ui->documentSize(doc, &width, &height)) {
      CenterX = static_cast<float>(width / 2);
      CenterY = static_cast<float>(height / 2);
    }
  }
}

void drawDefaultCrosshair() {
  AbcUiDocument *doc = panelDocument();
  AbcUiElement *spr = elementOf(doc, "spr-crosshair");
  if (!spr)
    return;
  // Old PaintBackground tail: only the vanilla "crosshair" cvar gate here -
  // the spectate/hudhide/suit/health gates already returned above.
  const bool vanillaOn = VanillaCrosshair && cvarInt(VanillaCrosshair, 0) > 0;
  AbcUiWeaponInfo weapon{};
  const bool haveWeapon = vanillaOn && CurrentWeaponId != 0 &&
                          Api->weapons->getById(CurrentWeaponId, &weapon) != 0;
  const AbcUiWeaponSprite *source = nullptr;
  if (haveWeapon) {
    AbcUiHudState state{};
    state.structSize = sizeof(state);
    Api->data->hudState(&state);
    // The module, not the registry, selects the game's default crosshair.
    const auto fallback = [](AbcUiWeaponSprite &sprite, bool target) {
      if (sprite.status == ABCUI_OK)
        return;
      sprite.status = ABCUI_OK;
      moduleui::copyCString(sprite.atlas, "crosshairs.spr");
      sprite.rect[0] = target ? 72 : 0;
      sprite.rect[1] = target ? 72 : 48;
      sprite.rect[2] = sprite.rect[0] + 24;
      sprite.rect[3] = sprite.rect[1] + 24;
    };
    fallback(weapon.crosshair, false);
    fallback(weapon.autoaim, true);
    if (weapon.zoomedCrosshair.status != ABCUI_OK)
      weapon.zoomedCrosshair = weapon.crosshair;
    if (weapon.zoomedAutoaim.status != ABCUI_OK)
      weapon.zoomedAutoaim = weapon.zoomedCrosshair;
    const bool onTarget = (weapon.state & kStateOnTarget) != 0;
    const float defaultFov = cvarValue(DefaultFov, 90.0f);
    const bool zoomed = state.fov < defaultFov;
    if (!zoomed) {
      source = (onTarget && (weapon.autoaim.status == ABCUI_OK))
                   ? &weapon.autoaim
                   : &weapon.crosshair;
    } else {
      source = (onTarget && (weapon.zoomedAutoaim.status == ABCUI_OK))
                   ? &weapon.zoomedAutoaim
                   : &weapon.zoomedCrosshair;
    }
  }
  // Old SetWeapon hid the panel outright on an unknown weapon / no vanilla
  // cvar / missing sprite, and vgui repainted the spr fresh every frame, so a
  // stale image could not linger - the Rml img needs an explicit hide
  // wherever this frame does not draw it.
  if (!haveWeapon || !source || source->status != ABCUI_OK) {
    SprUri.clear();
    Api->ui->elementSetProperty(spr, "display", "none");
    return;
  }

  const std::string uri =
      std::format("spralpha://{}/{},{},{},{}", source->atlas, source->rect[0],
                  source->rect[1], source->rect[2], source->rect[3]);
  const float w = static_cast<float>(source->rect[2] - source->rect[0]);
  const float h = static_cast<float>(source->rect[3] - source->rect[1]);

  if (SprUri != uri) {
    SprUri = uri;
    Api->ui->elementSetAttribute(spr, "src", uri.c_str());
  }
  const std::string left = pixelValue(CenterX - w / 2.0f);
  const std::string top = pixelValue(CenterY - h / 2.0f);
  const std::string width = pixelValue(w);
  const std::string height = pixelValue(h);
  Api->ui->elementSetProperty(spr, "display", "block");
  Api->ui->elementSetProperty(spr, "left", left.c_str());
  Api->ui->elementSetProperty(spr, "top", top.c_str());
  Api->ui->elementSetProperty(spr, "width", width.c_str());
  Api->ui->elementSetProperty(spr, "height", height.c_str());
}

// ---- event sinks ----

void onCurWeapon(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiCurWeaponEvent *>(payload);
  // Old SetWeapon feed (g_EventCurWeapon state>0): swap the sprite source;
  // state<=0 leaves the last weapon's spr (old parity - the panel kept
  // drawing the previous spr until Reset).
  if (event->state <= 0)
    return;
  AbcUiWeaponInfo weapon{};
  CurrentWeaponId = (Api->weapons->getById(event->id, &weapon) && event->id > 0)
                        ? event->id
                        : 0;
  SprUri.clear();
}

// ---- panel callbacks ----

void panelReset(void *) {
  // Old Reset: hide + forget the weapon/anim state.
  AbcUiDocument *doc = panelDocument();
  if (doc)
    Api->ui->documentHide(doc);
  CurrentWeaponId = 0;
  PreOffset = 0.0f;
  for (auto &part : Core)
    part.valid = false;
  for (auto &part : Outline)
    part.valid = false;
  SprUri.clear();
  hideDynamicCrosshair();
}

void panelOnRedraw(void *, double frameTime) {
  ClientTimeNow += frameTime;
  // Old OnThink + PaintBackground gates, evaluated live. Two D-17-registered
  // deviations from the event-driven old panel:
  // 1. Visibility is re-evaluated per frame instead of latched by
  //    SetWeapon/cl_crosshair callbacks - on spawn, before the first
  //    CurWeapon, the old panel stayed hidden while this one already draws
  //    the dynamic crosshair (the spr part still requires a known weapon).
  // 2. cl_crosshair_outline_draw off now forces transparent outlines every
  //    frame; the old code only wrote outline colors while the cvar was on,
  //    leaving the last color frozen on the panels after a live toggle-off.
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  AbcUiPlayerState player{};
  player.structSize = sizeof(player);
  Api->data->playerState(&player);

  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  const bool showDocument = state.suit && cvarInt(Crosshair, 1) > 0 &&
                            !(state.hideBits & ABCUI_HIDEBIT_ALL);
  if (showDocument)
    Api->ui->documentShow(doc);
  else
    Api->ui->documentHide(doc);
  if (!showDocument || player.index == 0)
    return;

  // Old OnThink/PaintBackground hide gates (spectate / hud hide / suit /
  // health): nothing draws this frame.
  if (state.spectate ||
      (state.hideBits & (ABCUI_HIDEBIT_ALL | ABCUI_HIDEBIT_WEAPONS)) ||
      !state.suit || state.health <= 0) {
    hideDynamicCrosshair();
    return;
  }
  updateCenter();
  // Zoom autohide: dynamic parts hide, the vanilla spr still draws (old
  // PaintBackground ran independently of this gate).
  if (VanillaCrosshair && cvarInt(VanillaCrosshair, 0) != 0 && AutoHide &&
      cvarValue(AutoHide, 0.0f) > 0.0f && DefaultFov &&
      cvarValue(DefaultFov, 90.0f) != state.fov) {
    hideDynamicCrosshair();
    drawDefaultCrosshair();
    return;
  }

  updateCenter();

  // Old OnThink dynamic geometry (raw pixels; drift from clientdata
  // punchangle - see the header port note for the EV capture).
  const float drift =
      (std::fabs(state.punchAngle[0]) + std::fabs(state.punchAngle[1])) * 5.0f *
      cvarValue(Multiple, 3.0f);
  const bool outlineOn = cvarInt(OutlineDraw, 1) > 0;
  AbcUiCrosshairLayout layout{};
  layout.thickness = cvarValue(Thickness, 2.0f);
  layout.length = cvarValue(Size, 24.0f);
  layout.offset = drift + cvarValue(Gap, 16.0f);
  layout.outlineWidth = outlineOn ? cvarValue(OutlineWidth, 1.0f) : 0.0f;
  layout.dot = cvarInt(Dot, 0) > 0 ? 1 : 0;
  layout.tShape = cvarInt(TShape, 0) > 0 ? 1 : 0;
  const float finalOffset = layout.offset + layout.thickness / 2.0f;

  char coreColor[64];
  char outlineColor[64];
  Api->ui->formatRgba(cvarInt(ColorR, 50), cvarInt(ColorG, 250),
                      cvarInt(ColorB, 50), cvarInt(Alpha, 200), coreColor,
                      sizeof(coreColor));
  Api->ui->formatRgba(cvarInt(OutlineR, 0), cvarInt(OutlineG, 0),
                      cvarInt(OutlineB, 0), cvarInt(OutlineAlpha, 255),
                      outlineColor, sizeof(outlineColor));

  // Old DrawDefaultCrosshair ran in PaintBackground (every frame, outside the
  // bounds throttle).
  drawDefaultCrosshair();

  // Old anim throttle: skip the bounds rewrite while inside the interval and
  // the gap did not grow.
  const double interval = cvarValue(AnimInterval, 0.1f);
  if (ClientTimeNow - AnimTime <= interval && PreOffset >= finalOffset)
    return;
  AnimTime = ClientTimeNow;
  PreOffset = finalOffset;

  // Shared with the settings preview: one geometry implementation, so the
  // preview cannot drift from what the game draws.
  float core[5][4];
  float outline[5][4];
  int32_t visible[5];
  Api->ui->crosshairLayout(&layout, core, outline, visible);
  for (int i = 0; i < 5; i++) {
    const bool shown = visible[i] != 0;
    const float coreBounds[4] = {CenterX + core[i][0], CenterY + core[i][1],
                                 core[i][2], core[i][3]};
    const float outlineBounds[4] = {CenterX + outline[i][0],
                                    CenterY + outline[i][1], outline[i][2],
                                    outline[i][3]};
    applyPart(elementOf(doc, kParts[i].core), Core[i],
              shown ? coreColor : kTransparent, coreBounds);
    applyPart(elementOf(doc, kParts[i].outline), Outline[i],
              (outlineOn && shown) ? outlineColor : kTransparent,
              outlineBounds);
  }
}

// ---- lifecycle ----

int onNativeHudDraw(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiNativeHudEvent *>(payload);
  return panelDocument() && (event->element == ABCUI_NATIVE_HUD_CROSSHAIR);
}

void onModuleLoad() {
  Api->events->subscribeVeto(ABCUI_CHANNEL_HUD_NATIVE_DRAW, 0, &onNativeHudDraw,
                             nullptr);
  Panel.structSize = sizeof(Panel);
  Panel.name = kPanelName;
  Panel.documentPath = kDocumentPath;
  Panel.zIndex = 0;
  Panel.userdata = nullptr;
  Panel.onDocumentReady = nullptr;
  Panel.onData = nullptr;
  Panel.onRedraw = &panelOnRedraw;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);

  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_CUR_WEAPON, 0, &onCurWeapon,
                         nullptr);
}

void onModuleInit() {
  Crosshair = Api->console->registerCvar(kCrosshair, "1", ABCUI_CVAR_VALUE);
  AutoHide = Api->console->registerCvar(kAutoHide, "1", ABCUI_CVAR_VALUE);
  Size = Api->console->registerCvar(kSize, "24", ABCUI_CVAR_VALUE);
  Thickness = Api->console->registerCvar(kThickness, "2", ABCUI_CVAR_VALUE);
  Gap = Api->console->registerCvar(kGap, "16", ABCUI_CVAR_VALUE);
  Multiple = Api->console->registerCvar(kMultiple, "3", ABCUI_CVAR_VALUE);
  Alpha = Api->console->registerCvar(kAlpha, "200", ABCUI_CVAR_VALUE);
  ColorR = Api->console->registerCvar(kColorR, "50", ABCUI_CVAR_VALUE);
  ColorG = Api->console->registerCvar(kColorG, "250", ABCUI_CVAR_VALUE);
  ColorB = Api->console->registerCvar(kColorB, "50", ABCUI_CVAR_VALUE);
  OutlineAlpha =
      Api->console->registerCvar(kOutlineAlpha, "255", ABCUI_CVAR_VALUE);
  OutlineR = Api->console->registerCvar(kOutlineR, "0", ABCUI_CVAR_VALUE);
  OutlineG = Api->console->registerCvar(kOutlineG, "0", ABCUI_CVAR_VALUE);
  OutlineB = Api->console->registerCvar(kOutlineB, "0", ABCUI_CVAR_VALUE);
  OutlineDraw = Api->console->registerCvar(kOutlineDraw, "1", ABCUI_CVAR_VALUE);
  OutlineWidth =
      Api->console->registerCvar(kOutlineWidth, "1", ABCUI_CVAR_VALUE);
  TShape = Api->console->registerCvar(kTShape, "0", ABCUI_CVAR_VALUE);
  Dot = Api->console->registerCvar(kDot, "0", ABCUI_CVAR_VALUE);
  AnimInterval =
      Api->console->registerCvar(kAnimInterval, "0.1", ABCUI_CVAR_VALUE);
  VanillaCrosshair = Api->console->findCvar("crosshair");
  DefaultFov = Api->console->findCvar("default_fov");
}

void onModuleShutdown() {
  // The document dies with the context; the part state must not leak into the
  // next cycle.
  for (auto &part : Core)
    part.valid = false;
  for (auto &part : Outline)
    part.valid = false;
  SprUri.clear();
}

void onModuleUnload() {
  Api = nullptr;
}

} // namespace

ABCUI_MODULE_EXPORT int ABCUI_CALL
AbcUiModule_Main(uint32_t abiVersion, const AbcUiModuleApi *api,
                 AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api ||
      !AbcUiModule_InitDescriptor(out))
    return 0;
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->panels ||
      !api->events || !api->ui || !api->console || !api->data || !api->weapons)
    return 0;
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->console->structSize < sizeof(AbcUiConsoleApi) ||
      api->data->structSize < sizeof(AbcUiDataApi) ||
      !ABCUI_API_HAS_MEMBER(AbcUiWeaponsApi, api->weapons, ammoIcon))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.crosshair";
  out->version = "2.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
