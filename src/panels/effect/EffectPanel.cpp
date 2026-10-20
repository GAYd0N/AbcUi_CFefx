// panels::EffectPanel implementation - see EffectPanel.h.
// from: ABCEnchance/src/hud/vgui/effect.* (GBK->UTF-8).
#include <metahook.h>

#include "EffectPanel.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>
#include <string>

#include <abc/CvarCmd.h>

#include "../../data/HudState.h"
#include "../../data/NetworkMessages.h"
#include "../../rmlui/animated/ElementAnimatedImage.h"

// Windows.h min/max macros fight std::min/std::max (old repo precedent).
#undef max
#undef min

namespace panels {

namespace effect {

// Old CEffectPanel ctor default (persisted contract - never rename).
cvar_t *HudEfx = nullptr;

void registerCvars() {
  HudEfx = abc::CREATE_CVAR("hud_efx", "1", abc::FCVAR_VALUE, nullptr);
}

} // namespace effect

namespace {

// Old EffectPanel.res timing.
constexpr float kFadeTime = 0.2f;      // fade_time (danger + under overlay)
constexpr float kWaterKeepTime = 1.5f; // water_keeptime (splash fade)

// Old WATERLEVEL enum (clientdata waterlevel values).
constexpr int kWaterLevelHead = 3;

} // namespace

EffectPanel::EffectPanel()
    : HudPanel("effect", "abc/abcui/hud/rml/effect.rml") {}

rmlui::animated::ElementAnimatedImage *
EffectPanel::splash(const char *elementId) const {
  Rml::Element *element = Document ? Document->GetElementById(elementId)
                                   : nullptr;
  return element ? rmlui_dynamic_cast<rmlui::animated::ElementAnimatedImage *>(
                       element)
                 : nullptr;
}

void EffectPanel::onDocumentReady() {
  // The splashes are <animimg> elements: the element owns the frame clock and
  // the texture, this controller only restarts them on the OUT_WATER edge.
  LeftSplash = splash("left-water");
  RightSplash = splash("right-water");
  subscribe(netmsg::Health, [this](int health) {
    // Old Viewport g_EventHealth handler ran before both panels' SetHealth:
    // while connected and not spectating, non-positive health clamps to 1.
    // Port note (D-17 deviation): the old clamp also required
    // PlayerResource's m_bIsConnected, which batch C has not landed yet -
    // the local-player + spectate checks stand in until then.
    if (health <= 0) {
      const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
      if (local && !hudstate::isSpectate())
        health = 1;
    }
    setHealth(health);
  });
}

void EffectPanel::reset() {
  // Old Reset: panel shown, all overlays hidden, water state cleared.
  OldWaterStatus = 0;
  NowWaterType = WaterType::None;
  OutWaterStarted = false;
  if (LeftSplash)
    LeftSplash->setPlaying(false);
  if (RightSplash)
    RightSplash->setPlaying(false);
  if (Document) {
    for (const char *id :
         {"left-water", "right-water", "under-water", "danger"}) {
      if (Rml::Element *el = Document->GetElementById(id)) {
        el->SetProperty("display", "none");
        el->RemoveProperty("transition");
      }
    }
  }
  show();
}

void EffectPanel::setHealth(int health) {
  // Old SetHealth: below hud_danger_health, ramp the danger vignette toward
  // (1 - health/threshold) * 255; at/above, hide it. The RunAnimationCommand
  // from-current-value semantic is Rml's opacity transition (0.2s linear) -
  // every Health message re-targets it, matching the old re-run.
  Rml::Element *danger =
      Document ? Document->GetElementById("danger") : nullptr;
  if (!danger)
    return;
  // Old gCVars.pDangerHealth - one engine cvar shared by the health and
  // effect panels (owned by the health namespace's registerCvars).
  const auto *dangerCvar = abc::CVAR_GET_POINTER("hud_danger_health");
  const float dangerValue = dangerCvar ? dangerCvar->value : 45.0f;
  if (dangerValue > 0.0f && health < dangerValue) {
    const float target = 1.0f - static_cast<float>(health) / dangerValue;
    danger->SetProperty("display", "block");
    danger->SetProperty("transition", "opacity 0.2s linear-in");
    danger->SetProperty("opacity", std::to_string(target));
  } else {
    danger->SetProperty("display", "none");
    danger->SetProperty("opacity", "0");
  }
}

void EffectPanel::setWater(WaterType type) {
  if (NowWaterType == type)
    return;
  NowWaterType = type;
  OutWaterStart = ClientTimeNow;
  // Old SetWater(OUT_WATER): both splashes restart at frame 0 / alpha 255 in
  // the same frame as the transition.
  OutWaterStarted = type == WaterType::OutWater;
}

void EffectPanel::onData(double frameTime) {
  ClientTimeNow += frameTime;
  if (!Document)
    return;

  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  // Old OnThink gate: no local player / spectate / hud_efx < 1 hides the
  // panel; else it shows. The Viewport-wide kHideAll token hides it too.
  const bool active =
      local && (!effect::HudEfx || effect::HudEfx->value >= 1.0f) &&
      !hudstate::isSpectate() && !hudstate::isHudHidden(hudstate::kHideAll);
  if (!active) {
    hide();
    return;
  }
  show();

  Rml::Element *left = Document->GetElementById("left-water");
  Rml::Element *right = Document->GetElementById("right-water");
  Rml::Element *under = Document->GetElementById("under-water");
  if (!left || !right || !under)
    return;

  // Old water state machine: leaving head-depth fires OUT_WATER once; while
  // the splashes are still fading (alpha > 0) the type holds, otherwise the
  // state resets to NONE (old alpha poll). Dipping back under restarts the
  // UNDER_WATER fade-in.
  const int waterlevel = hudstate::waterlevel();
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

  switch (NowWaterType) {
  case WaterType::None: {
    // Old SetWater(NONE): all three hidden outright.
    left->SetProperty("display", "none");
    right->SetProperty("display", "none");
    under->SetProperty("display", "none");
    break;
  }
  case WaterType::UnderWater: {
    // Old: splashes hidden; under overlay fades in over fade_time.
    left->SetProperty("display", "none");
    right->SetProperty("display", "none");
    const float alpha = std::min(
        1.0f, static_cast<float>(ClientTimeNow - OutWaterStart) / kFadeTime);
    under->SetProperty("display", "block");
    under->SetProperty("opacity", std::to_string(alpha));
    break;
  }
  case WaterType::OutWater: {
    // Old: splashes restart at frame 0 / alpha 255 and fade over
    // water_keeptime; the under overlay fades out over fade_time. The frame
    // sequence (old CSPRImage Animate, 10 fps) is the element's; the panel
    // restarts it once on the transition frame.
    if (OutWaterStarted) {
      OutWaterStarted = false;
      if (LeftSplash)
        LeftSplash->Restart();
      if (RightSplash)
        RightSplash->Restart();
    }
    const float splashAlpha = std::max(
        0.0f, 1.0f - static_cast<float>(ClientTimeNow - OutWaterStart) /
                         kWaterKeepTime);
    const float underAlpha = std::max(
        0.0f,
        1.0f - static_cast<float>(ClientTimeNow - OutWaterStart) / kFadeTime);
    left->SetProperty("display", "block");
    left->SetProperty("opacity", std::to_string(splashAlpha));
    right->SetProperty("display", "block");
    right->SetProperty("opacity", std::to_string(splashAlpha));
    under->SetProperty("display", "block");
    under->SetProperty("opacity", std::to_string(underAlpha));
    break;
  }
  }
}

} // namespace panels
