// panels::IndicatorPanel implementation - see IndicatorPanel.h.
// from: ABCEnchance/src/hud/vgui/indicator.* (GBK->UTF-8).
#include <metahook.h>

#include "IndicatorPanel.h"
#include "../../data/HudState.h"

#include <RmlUi/Core/Element.h>

#include <cmath>
#include <format>
#include <cstdio>

#include <abc/CvarCmd.h>
#include <abc/Math.h>

#include "../../data/HudState.h"
#include "../../data/NetworkMessages.h"

namespace panels {

namespace indicator {

// Old CIndicatorPanel ctor defaults (persisted contract - never rename).
// hud_damageshock* were created-but-unused in the old ctor as well (the
// damage-shock feature predated the IndicatorImage rework) - kept for cvar
// surface parity, no consumer.
cvar_t *IndicatorStyle = nullptr;
cvar_t *DamageShock = nullptr;
cvar_t *DamageShockFactor = nullptr;
cvar_t *DamageShockBase = nullptr;

// Old IndicatorPanel.res timing.
constexpr float kKeepTime = 2.0f;
constexpr float kFadeTime = 1.0f;

void registerCvars() {
  IndicatorStyle =
      abc::CREATE_CVAR("hud_indicator_style", "2", abc::FCVAR_VALUE, nullptr);
  DamageShock =
      abc::CREATE_CVAR("hud_damageshock", "1", abc::FCVAR_VALUE, nullptr);
  DamageShockFactor = abc::CREATE_CVAR("hud_damageshock_factor", "0.015",
                                       abc::FCVAR_VALUE, nullptr);
  DamageShockBase =
      abc::CREATE_CVAR("hud_damageshock_base", "15", abc::FCVAR_VALUE, nullptr);
}

} // namespace indicator

namespace {

// Old IndicatorStyle enum: Hide=0, GoldSrc=1 (quantized quadrants), New=2.
constexpr int kStyleHide = 0;
constexpr int kStyleGoldSrc = 1;
constexpr int kStyleNew = 2;

// Slot rotor element ids (old IndicatorImage1..4). The per-slot offsets
// (30/40/50/50) are baked into the RCSS slot positions.
constexpr const char *kRotorIds[4] = {
    "rotor-1",
    "rotor-2",
    "rotor-3",
    "rotor-4",
};

} // namespace

IndicatorPanel::IndicatorPanel()
    : HudPanel("indicator", "abc/abcui/hud/rml/indicator.rml") {}

void IndicatorPanel::onDocumentReady() {
  subscribe(netmsg::Damage,
            [this](const netmsg::DamageData &data) { onHit(data.from); });
}

void IndicatorPanel::reset() {
  // Old Reset: clear slots + hide all rotors.
  for (Slot &slot : Slots) {
    slot.startTime = -1.0;
    slot.appliedAngle.clear();
  }
  NextSlot = 0;
  hide();
  if (Document) {
    for (const char *id : kRotorIds) {
      if (Rml::Element *rotor = Document->GetElementById(id))
        rotor->SetProperty("display", "none");
    }
  }
}

void IndicatorPanel::onHit(const float from[3]) {
  // Old SetHitIndicator: claim the next cycling slot, restart its clock.
  Slot &slot = Slots[NextSlot];
  for (int i = 0; i < 3; i++)
    slot.hitFrom[i] = from[i];
  slot.startTime = ClientTimeNow;
  NextSlot = (NextSlot + 1) % kSlots;
  if (!isVisible())
    show();
}

void IndicatorPanel::onData(double frameTime) {
  ClientTimeNow += frameTime;
  if (hudstate::isHudHidden(hudstate::kHideAll)) {
    hide();
    return;
  }

  const int style = indicator::IndicatorStyle
                        ? static_cast<int>(indicator::IndicatorStyle->value)
                        : kStyleNew;
  bool anyActive = false;
  for (int i = 0; i < kSlots; i++) {
    Slot &slot = Slots[i];
    Rml::Element *rotor =
        Document ? Document->GetElementById(kRotorIds[i]) : nullptr;
    if (!rotor)
      continue;
    if (slot.startTime < 0.0)
      continue;

    const float elapsed = static_cast<float>(ClientTimeNow - slot.startTime);
    if (elapsed >= indicator::kKeepTime + indicator::kFadeTime) {
      // Old OnThink completion path: hide + reset the clock.
      slot.startTime = -1.0;
      slot.appliedAngle.clear();
      rotor->SetProperty("display", "none");
      continue;
    }

    const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
    if (!local)
      continue;
    if (style <= kStyleHide) {
      // Old IndicatorImage::Paint hid every draw under the style gate;
      // the rotors vanish with it.
      rotor->SetProperty("display", "none");
      continue;
    }

    // Relative yaw of the hit source vs the local player's entity yaw
    // (old IndicatorImage::Paint math, verbatim).
    float rel[3] = {
        slot.hitFrom[0] - local->curstate.origin[0],
        slot.hitFrom[1] - local->curstate.origin[1],
        slot.hitFrom[2] - local->curstate.origin[2],
    };
    abc::CMathlib::VectorAngles(rel, rel);
    rel[abc::CMathlib::Q_YAW] -= local->curstate.angles[abc::CMathlib::Q_YAW];
    float degAngle = fmodf(rel[abc::CMathlib::Q_YAW], 360.0f);
    if (degAngle < 0.0f)
      degAngle += 360.0f;
    if (style == kStyleGoldSrc) {
      // Old GoldSrc quadrant quantization.
      if ((degAngle >= 0.0f && degAngle <= 45.0f) ||
          (degAngle >= 315.0f && degAngle <= 360.0f))
        degAngle = 0.0f;
      else if (degAngle > 45.0f && degAngle <= 135.0f)
        degAngle = 90.0f;
      else if (degAngle > 135.0f && degAngle <= 225.0f)
        degAngle = 180.0f;
      else
        degAngle = 270.0f;
    }

    const std::string angle = std::format("rotate({:.1f}deg)", degAngle);
    if (slot.appliedAngle != angle) {
      slot.appliedAngle = angle;
      rotor->SetProperty("transform", angle);
    }

    // Old alpha ramp: hold 255 for keep_time, linear fade over fade_time.
    const float alpha =
        elapsed <= indicator::kKeepTime
            ? 1.0f
            : 1.0f - (elapsed - indicator::kKeepTime) / indicator::kFadeTime;
    rotor->SetProperty("display", "block");
    rotor->SetProperty("opacity", std::to_string(alpha));
    anyActive = true;
  }
  if (!anyActive)
    hide();
}

} // namespace panels
