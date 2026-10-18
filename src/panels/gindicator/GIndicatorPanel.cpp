// panels::GIndicatorPanel implementation - see GIndicatorPanel.h.
// from: ABCEnchance/src/hud/vgui/gindicator.* (GBK->UTF-8).
#include <metahook.h>

#include "GIndicatorPanel.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cmath>
#include <vector>

#include <com_model.h>

#include <abc/CvarCmd.h>
#include <abc/Console.h>
#include <abc/Math.h>

#include "../../data/HudState.h"
#include "../../framework/Projection.h"

namespace panels {

namespace {

// Old cos-60 front-facing gate (shared by the spine seam and onData).
constexpr float kFrontDotGate = 0.5f;

} // namespace

namespace gindicator {

// Old CGenadeIndicatorPanel ctor defaults (persisted contract - never rename).
cvar_t *GrenadeIndicator = nullptr;
cvar_t *GrenadeIndicatorRange = nullptr;
cvar_t *GrenadeIndicatorTime = nullptr;

void registerCvars() {
  GrenadeIndicator =
      abc::CREATE_CVAR("hud_indicator_grenade", "1", abc::FCVAR_VALUE, nullptr);
  GrenadeIndicatorRange = abc::CREATE_CVAR("hud_indicator_grenade_range", "256",
                                           abc::FCVAR_VALUE, nullptr);
  GrenadeIndicatorTime = abc::CREATE_CVAR("hud_indicator_grenade_time", "4",
                                          abc::FCVAR_VALUE, nullptr);
}

namespace {

// Old CGIndicatorItem: engine-owned entity pointer + expiry clock.
struct TrackedGrenade {
  cl_entity_t *ent = nullptr;
  float expire = 0.0f;
  Rml::Element *marker = nullptr;
};

// File-static tracked list (spine seam, 09 §6 shape). The entity pointers are
// engine-owned - only compared/dereferenced between AddEntity and expiry.
std::vector<TrackedGrenade> Tracked;

constexpr const char *kGrenadeModelPath = "models/w_grenade.mdl";

} // namespace

void onAddEntity(cl_entity_t *ent, const char *modelname) {
  if (!GrenadeIndicator || GrenadeIndicator->value <= 0)
    return;
  // D-17: the old gate also required curstate.sequence != 0 - on SC 5.26
  // the grenade throw entities carry sequence 0 for their whole flight, so
  // the old code never drew a marker (trace evidence, 08 §4.32). The
  // sequence gate is dropped; model name / studio type / messagenum /
  // range gates stay.
  if (!ent || !ent->model || strcmp(modelname, kGrenadeModelPath) != 0 ||
      ent->model->type != mod_studio)
    return;
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (!local || ent->curstate.messagenum != local->curstate.messagenum)
    return;
  float length[3] = {
      local->curstate.origin[0] - ent->curstate.origin[0],
      local->curstate.origin[1] - ent->curstate.origin[1],
      local->curstate.origin[2] - ent->curstate.origin[2],
  };
  if (sqrtf(length[0] * length[0] + length[1] * length[1] +
            length[2] * length[2]) >
      (GrenadeIndicatorRange ? GrenadeIndicatorRange->value : 256.0f))
    return;
  for (const TrackedGrenade &tracked : Tracked) {
    if (tracked.ent == ent)
      return;
  }
  TrackedGrenade tracked;
  tracked.ent = ent;
  tracked.expire = gEngfuncs.GetClientTime() +
                   (GrenadeIndicatorTime ? GrenadeIndicatorTime->value : 4.0f);
  Tracked.push_back(tracked);
}

} // namespace gindicator

GIndicatorPanel::GIndicatorPanel()
    : HudPanel("gindicator", "abc/abcui/hud/rml/gindicator.rml") {}

void GIndicatorPanel::onDocumentReady() {}

void GIndicatorPanel::reset() {
  // Old Reset: ShowPanel(true) + clear the list (the map change invalidates
  // every tracked entity pointer anyway).
  gindicator::Tracked.clear();
  if (Document) {
    if (Rml::Element *container = Document->GetElementById("markers")) {
      while (Rml::Element *child = container->GetFirstChild())
        container->RemoveChild(child);
    }
  }
  show();
}

void GIndicatorPanel::onData(double frameTime) {
  (void)frameTime;
  // The per-frame body runs in onRedraw (world projection needs the live
  // view matrices; old PaintBackground ran in the same phase).
}

void GIndicatorPanel::onRedraw(double frameTime) {
  (void)frameTime;
  Rml::Element *container =
      Document ? Document->GetElementById("markers") : nullptr;
  if (!container)
    return;

  // Old Viewport-wide visibility (kHideAll token hides everything).
  if (hudstate::isHudHidden(hudstate::kHideAll))
    hide();
  else
    show();

  // Old OnThink expiry sweep runs regardless of the paint gates.
  for (size_t i = 0; i < gindicator::Tracked.size();) {
    gindicator::TrackedGrenade &tracked = gindicator::Tracked[i];
    const cl_entity_t *localNow = gEngfuncs.GetLocalPlayer();
    const auto *ent = tracked.ent;
    const bool expired =
        !gindicator::GrenadeIndicator ||
        gindicator::GrenadeIndicator->value <= 0 || !ent || !ent->model ||
        strcmp(ent->model->name, gindicator::kGrenadeModelPath) != 0 ||
        !localNow ||
        ent->curstate.messagenum != localNow->curstate.messagenum ||
        (ent->curstate.effects & EF_NODRAW) ||
        tracked.expire <= gEngfuncs.GetClientTime();
    if (expired) {
      if (tracked.marker)
        container->RemoveChild(tracked.marker);
      gindicator::Tracked.erase(gindicator::Tracked.begin() + i);
      continue;
    }
    i++;
  }

  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (!local)
    return;

  // View forward (old PaintBackground angles math).
  vec3_t view{}, forward{};
  gEngfuncs.GetViewAngles(view);
  abc::math::AngleVectors(view, forward, nullptr, nullptr);

  for (gindicator::TrackedGrenade &tracked : gindicator::Tracked) {
    if (!tracked.marker) {
      Rml::ElementPtr holder = Document->CreateElement("img");
      if (!holder)
        return;
      tracked.marker = holder.get();
      tracked.marker->SetClass("grenade", true);
      tracked.marker->SetAttribute("src",
                                   Rml::String("../img/grenadeicon.png"));
      container->AppendChild(std::move(holder));
    }

    // Front-facing gate (old dot > 0.5) then project.
    float rel[3] = {
        tracked.ent->curstate.origin[0] - local->curstate.origin[0],
        tracked.ent->curstate.origin[1] - local->curstate.origin[1],
        tracked.ent->curstate.origin[2] - local->curstate.origin[2],
    };
    const float length =
        sqrtf(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
    const float dot = length > 0.0f
                          ? (rel[0] * forward[0] + rel[1] * forward[1] +
                             rel[2] * forward[2]) /
                                length
                          : 0.0f;
    if (dot > kFrontDotGate &&
        length <= (gindicator::GrenadeIndicatorRange
                       ? gindicator::GrenadeIndicatorRange->value
                       : 256.0f)) {
      float screen[2] = {};
      framework::projection::worldToScreen(tracked.ent->curstate.origin,
                                           screen);
      // Old 24x24 box centered on the projection.
      tracked.marker->SetProperty("display", "block");
      tracked.marker->SetProperty("left",
                                  std::to_string(screen[0] - 12.0f) + "px");
      tracked.marker->SetProperty("top",
                                  std::to_string(screen[1] - 12.0f) + "px");
    } else {
      tracked.marker->SetProperty("display", "none");
    }
  }
}

} // namespace panels
