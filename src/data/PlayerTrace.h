#pragma once

// data::playertrace - the player's cached hull traces (view / head / foot).
// from: ABCEnchance/src/core/module/playertrace.* (GBK->UTF-8).
//
// Lives in the data layer (plan 05 §4.5): the hook spine pumps update() once
// per HUD_Frame (old exportfuncs HUD_Frame tail), consumers read the cached
// traces wherever they need them - the crosshair's third-person centre reads
// the view trace's end position in the render phase.
//
// Old semantics preserved: the view trace is a hull-2 box trace from the eye
// along the view axis (PM_STUDIO_BOX, local player skipped); head and foot
// are world-only vertical traces from the player origin, and the hull set
// for the view trace deliberately carries over to both (old code set it once
// per update). The whole update is skipped while the HUD is hidden (old
// `GetBaseViewPort()->IsVisible()` gate = the hide-all token).

#include <pm_defs.h>

namespace playertrace {

enum class TraceType {
  View,
  Head,
  Foot,
};

// HUD_Frame pump (old GetPlayerTrace()->Update()): no-op without a local
// player, while the hide-all token is up, or while the engine has no live
// level. `levelActive` is the engine-confirmed level fact the GameUI
// lifecycle feeds (the spine passes nativeUiBridge().hasLevel()): every
// trace below walks the world's BSP, and while a map change replaces that
// world the engine's own trace walks freed clipnode/plane data - measured
// as an access violation inside EV_PlayerTrace during a map change.
void update(bool levelActive);

// Cached trace; never null (an update that has not run yet yields the
// zero-initialised trace, exactly like the old module before its first
// Update).
const pmtrace_t *get(TraceType type);

} // namespace playertrace
