// abcui.hudeffects - screen effects: the danger vignette and the water
// overlays. Moved from src/panels/effect (old CEffectPanel, ABCEnchance
// sources: src/hud/vgui/effect.*).
//
// The two ImageSprPanel water splashes (face_rainleft/right, additive, 10 fps)
// are <animimg> elements playing animated WebP files of the same frames; the
// element owns the frame clock and the texture, this controller only restarts
// the sequence on the OUT_WATER edge and drives the alpha ramps.
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <algorithm>
#include <cstdio>
#include <format>
#include <string>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};

// Old CEffectPanel ctor default (persisted contract - never rename).
constexpr const char *kHudEfx = "hud_efx";

constexpr const char *kPanelName = "effect";
constexpr const char *kDocumentPath =
    "mod://abcui.hudeffects/hud/rml/effect.rml";

// Old EffectPanel.res timing.
constexpr double kFadeTime = 0.2;      // fade_time (danger + under overlay)
constexpr double kWaterKeepTime = 1.5; // water_keeptime (splash fade)

// Old WATERLEVEL enum (clientdata waterlevel values).
constexpr int kWaterLevelHead = 3;

// Old CEffectPanel::WaterType.
enum class WaterType {
  None = 0,
  UnderWater = 1,
  OutWater = 12,
};

AbcUiCvarHandle HudEfx = nullptr;

int OldWaterStatus = 0;
WaterType NowWaterType = WaterType::None;
double OutWaterStart = 0.0;
// Client clock for the alpha ramps (the frame clock lives in the element).
double ClientTimeNow = 0.0;
// The splash elements replay from frame 0 once per OUT_WATER transition -
// the old CSPRImage SetFrame(0) restart.
bool OutWaterStarted = false;

AbcUiDocument *panelDocument() {
  return Api->ui->panelDocument(kPanelName);
}

void setVisible(bool visible) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (visible)
    Api->ui->documentShow(doc);
  else
    Api->ui->documentHide(doc);
}

void setElementProperty(const char *id, const char *name, const char *value) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (AbcUiElement *element = Api->ui->querySelector(doc, id))
    Api->ui->elementSetProperty(element, name, value);
}

void restartSplash(const char *id) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (AbcUiElement *element = Api->ui->querySelector(doc, id))
    Api->ui->elementRestartAnimation(element);
}

void setHealth(int health) {
  // Old SetHealth: below hud_danger_health, ramp the danger vignette toward
  // (1 - health/threshold) * 255; at/above, hide it. The RunAnimationCommand
  // from-current-value semantic is Rml's opacity transition (0.2s linear) -
  // every Health message re-targets it, matching the old re-run.
  if (!panelDocument())
    return;
  // Old gCVars.pDangerHealth - one engine cvar shared by the health and
  // effect panels; the health module owns the registration.
  const AbcUiCvarHandle dangerCvar =
      Api->console->findCvar("hud_danger_health");
  const float dangerValue =
      dangerCvar ? Api->console->cvarValue(dangerCvar, 45.0f) : 45.0f;
  if (dangerValue > 0.0f && health < static_cast<int>(dangerValue)) {
    const float target = 1.0f - static_cast<float>(health) / dangerValue;
    const std::string opacity = std::format("{:g}", target);
    setElementProperty("danger", "display", "block");
    setElementProperty("danger", "transition", "opacity 0.2s linear-in");
    setElementProperty("danger", "opacity", opacity.c_str());
  } else {
    setElementProperty("danger", "display", "none");
    setElementProperty("danger", "opacity", "0");
  }
}

void setWater(WaterType type) {
  if (NowWaterType == type)
    return;
  NowWaterType = type;
  OutWaterStart = ClientTimeNow;
  // Old SetWater(OUT_WATER): both splashes restart at frame 0 / alpha 255 in
  // the same frame as the transition.
  OutWaterStarted = type == WaterType::OutWater;
}

// ---- event sinks ----

void onHealth(void *, const void *payload) {
  int health = static_cast<const AbcUiIntEvent *>(payload)->value;
  // Old Viewport g_EventHealth handler ran before both panels' SetHealth:
  // while connected and not spectating, non-positive health clamps to 1.
  if (health <= 0) {
    AbcUiHudState state{};
    state.structSize = sizeof(state);
    Api->data->hudState(&state);
    AbcUiPlayerState player{};
    player.structSize = sizeof(player);
    Api->data->playerState(&player);
    if (player.index != 0 && !state.spectate)
      health = 1;
  }
  setHealth(health);
}

// ---- panel callbacks ----

void panelReset(void *) {
  // Old Reset: panel shown, all overlays hidden, water state cleared.
  OldWaterStatus = 0;
  NowWaterType = WaterType::None;
  OutWaterStarted = false;
  for (const char *id :
       {"left-water", "right-water", "under-water", "danger"}) {
    setElementProperty(id, "display", "none");
    setElementProperty(id, "transition", "");
  }
  setVisible(true);
}

void panelOnData(void *, double frameTime) {
  ClientTimeNow += frameTime;
  if (!panelDocument())
    return;

  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  AbcUiPlayerState player{};
  player.structSize = sizeof(player);
  Api->data->playerState(&player);

  // Old OnThink gate: no local player / spectate / hud_efx < 1 hides the
  // panel; else it shows. The Viewport-wide kHideAll token hides it too.
  const bool enabled = !HudEfx || Api->console->cvarValue(HudEfx, 1.0f) >= 1.0f;
  const bool active = player.index != 0 && enabled && !state.spectate &&
                      !(state.hideBits & ABCUI_HIDEBIT_ALL);
  if (!active) {
    setVisible(false);
    return;
  }
  setVisible(true);

  // Old water state machine: leaving head-depth fires OUT_WATER once; while
  // the splashes are still fading (alpha > 0) the type holds, otherwise the
  // state resets to NONE (old alpha poll). Dipping back under restarts the
  // UNDER_WATER fade-in.
  const int waterlevel = state.waterLevel;
  if (OldWaterStatus == kWaterLevelHead) {
    setWater(waterlevel == kWaterLevelHead ? WaterType::UnderWater
                                           : WaterType::OutWater);
  } else if (NowWaterType == WaterType::OutWater &&
             ClientTimeNow - OutWaterStart < kWaterKeepTime) {
    // Splash still fading - hold the OUT state.
  } else {
    setWater(WaterType::None);
  }
  OldWaterStatus = waterlevel;

  std::string alpha;
  switch (NowWaterType) {
  case WaterType::None:
    // Old SetWater(NONE): all three hidden outright.
    setElementProperty("left-water", "display", "none");
    setElementProperty("right-water", "display", "none");
    setElementProperty("under-water", "display", "none");
    break;
  case WaterType::UnderWater: {
    // Old: splashes hidden; under overlay fades in over fade_time.
    setElementProperty("left-water", "display", "none");
    setElementProperty("right-water", "display", "none");
    const double ratio = (ClientTimeNow - OutWaterStart) / kFadeTime;
    alpha = std::format("{:g}", std::min(1.0, ratio));
    setElementProperty("under-water", "display", "block");
    setElementProperty("under-water", "opacity", alpha.c_str());
    break;
  }
  case WaterType::OutWater: {
    // Old: splashes restart at frame 0 / alpha 255 and fade over
    // water_keeptime; the under overlay fades out over fade_time. The frame
    // sequence (old CSPRImage Animate, 10 fps) is the element's; the panel
    // restarts it once on the transition frame.
    if (OutWaterStarted) {
      OutWaterStarted = false;
      restartSplash("left-water");
      restartSplash("right-water");
    }
    const double elapsed = ClientTimeNow - OutWaterStart;
    const double splashAlpha = std::max(0.0, 1.0 - elapsed / kWaterKeepTime);
    const double underAlpha = std::max(0.0, 1.0 - elapsed / kFadeTime);
    alpha = std::format("{:g}", splashAlpha);
    setElementProperty("left-water", "display", "block");
    setElementProperty("left-water", "opacity", alpha.c_str());
    setElementProperty("right-water", "display", "block");
    setElementProperty("right-water", "opacity", alpha.c_str());
    alpha = std::format("{:g}", underAlpha);
    setElementProperty("under-water", "display", "block");
    setElementProperty("under-water", "opacity", alpha.c_str());
    break;
  }
  }
}

// ---- lifecycle ----

void onModuleLoad() {
  Panel.structSize = sizeof(Panel);
  Panel.name = kPanelName;
  Panel.documentPath = kDocumentPath;
  Panel.zIndex = 0;
  Panel.userdata = nullptr;
  Panel.onDocumentReady = nullptr;
  Panel.onData = &panelOnData;
  Panel.onRedraw = nullptr;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);

  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_HEALTH, 0, &onHealth, nullptr);
}

void onModuleInit() {
  HudEfx = Api->console->registerCvar(kHudEfx, "1", ABCUI_CVAR_VALUE);
}

void onModuleShutdown() {
  // The document dies with the context; the water state restarts clean.
  OldWaterStatus = 0;
  NowWaterType = WaterType::None;
  OutWaterStarted = false;
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
      !api->events || !api->ui || !api->console || !api->data)
    return 0;
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->console->structSize < sizeof(AbcUiConsoleApi) ||
      api->data->structSize < sizeof(AbcUiDataApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.hudeffects";
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
