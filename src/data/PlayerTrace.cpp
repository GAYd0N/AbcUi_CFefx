// data::playertrace implementation - see PlayerTrace.h.
// from: ABCEnchance/src/core/module/playertrace.cpp (GBK->UTF-8).
#include <metahook.h>

#include "PlayerTrace.h"

#include <event_api.h>
#include <pm_defs.h>

#include <abc/Math.h>

#include "HudState.h"

namespace playertrace {

namespace {

pmtrace_t ViewTrace = {};
pmtrace_t HeadTrace = {};
pmtrace_t FootTrace = {};

} // namespace

void update(bool levelActive) {
  // Old CPlayerTrace::Update gate: only while the HUD is up (the old
  // Viewport-visible check; here the equivalent hide-all token).
  if (hudstate::isHudHidden(hudstate::kHideAll))
    return;
  // No live level, no world to trace into (see the header contract): a
  // load in progress has a replaced world, whose clipnode/plane data the
  // engine's trace would walk as freed memory.
  if (!levelActive)
    return;
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (!local)
    return;

  // View: hull-2 box trace from the eye 8192u along the view axis (old
  // vp block). EV_SetTraceHull(2) stays in force for the head/foot traces
  // below - old behaviour, deliberately not "fixed". The end point hangs
  // off the *feet* origin (old VectorMA on vecSrc), only the start point
  // carries the view height - kept byte-for-byte.
  vec3_t angles{};
  gEngfuncs.GetViewAngles(angles);
  vec3_t forward{};
  abc::CMathlib::AngleVectors(angles, forward, nullptr, nullptr);
  vec3_t viewOfs{};
  gEngfuncs.pEventAPI->EV_LocalPlayerViewheight(viewOfs);
  vec3_t start{};
  for (int i = 0; i < 3; i++)
    start[i] = local->origin[i] + viewOfs[i];
  vec3_t end{};
  for (int i = 0; i < 3; i++)
    end[i] = local->origin[i] + forward[i] * 8192.0f;
  int ignorePhysent = -1;
  if (gEngfuncs.pEventAPI->EV_GetPhysent)
    for (int i = 0; i < MAX_PHYSENTS; ++i) {
      const auto *phys = gEngfuncs.pEventAPI->EV_GetPhysent(i);
      if (!phys)
        break;
      if (phys->info == local->index) {
        ignorePhysent = i;
        break;
      }
    }
  gEngfuncs.pEventAPI->EV_SetTraceHull(2);
  gEngfuncs.pEventAPI->EV_PlayerTrace(start, end, PM_STUDIO_BOX, ignorePhysent,
                                      &ViewTrace);

  // Head: world-only trace straight up from the player origin.
  vec3_t up{0.0f, 0.0f, 8192.0f};
  vec3_t upEnd{};
  for (int i = 0; i < 3; i++)
    upEnd[i] = local->origin[i] + up[i];
  gEngfuncs.pEventAPI->EV_PlayerTrace(const_cast<float *>(local->origin), upEnd,
                                      PM_WORLD_ONLY, -1, &HeadTrace);

  // Foot: world-only trace straight down (old module parity).
  vec3_t down{0.0f, 0.0f, -8192.0f};
  vec3_t downEnd{};
  for (int i = 0; i < 3; i++)
    downEnd[i] = local->origin[i] + down[i];
  gEngfuncs.pEventAPI->EV_PlayerTrace(const_cast<float *>(local->origin),
                                      downEnd, PM_WORLD_ONLY, -1, &FootTrace);
}

const pmtrace_t *get(TraceType type) {
  switch (type) {
  case TraceType::Head:
    return &HeadTrace;
  case TraceType::Foot:
    return &FootTrace;
  case TraceType::View:
  default:
    return &ViewTrace;
  }
}

} // namespace playertrace
