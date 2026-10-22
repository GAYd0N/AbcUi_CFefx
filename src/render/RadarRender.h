#pragma once

// framework::radarrender - the radar's world-to-FBO producer (route 2).
//
// The radar panel is a UI module, but rendering the world from above is a
// renderer-facing concern and stays in the framework (the same rule that
// keeps the render bridge, video decode and avatar textures framework-side).
// This service owns the 1024^2 FBO, the meta-renderer callbacks (including
// the frame-buffer regeneration seam) and the square/round mask textures, and
// registers the FBO colour texture with AbcRenderInterface as the external
// "radarfbo://" texture family.
//
// The module drives it through the SDK `radar` section: setActive(true) while
// the plate is on screen, setMaskRound() for the plate shape, and a
// generation counter it appends to the texture URI (RmlUi caches textures per
// source string, so a regenerated FBO needs a new suffix).

namespace framework {

namespace radarrender {

// Starts the producer lazily (first call) and keeps it registered for the
// process lifetime; `active` false skips the world pass entirely.
void setActive(bool active);

// Selects the mask texture the external-texture draw applies (square/round).
void setMaskRound(bool round);

// Orthographic zoom (the hud_radar_zoom cvar): the visible span is
// 2 * (4096 / zoom) world units.
void setZoom(float zoom);

// True when the FBO colour texture exists and is registered.
bool ready();

// Bumped whenever the FBO texture handle changes (creation / regeneration).
int generation();

} // namespace radarrender

} // namespace framework
