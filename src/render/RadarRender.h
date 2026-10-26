#pragma once

// framework::radarrender - the radar's world-to-FBO producer (route 2).
//
// The radar panel is a UI module, but rendering the world from above is a
// render-backend concern and runs through the backend's world-pass
// capability (abc::render::IWorldPass, issue #24): the backend owns the
// 1024^2 target, the frame beat and the GPU pass; this service keeps the
// orchestration (cvar save/restore, camera math, zoom), a borrowed
// module-uploaded mask and the external-texture registration of the target
// colour texture as "radarfbo://".
//
// The module drives it through the SDK `radar` section: setActive(true)
// while the plate is on screen, setMask() for the plate shape, and a
// generation counter it appends to the texture URI (RmlUi caches textures
// per source string, so a regenerated FBO needs a new suffix).

namespace framework {

namespace radarrender {

// The backend's world-render beat (Renderer: OnPreRenderView). Runs the
// pass while the plate is active; called from the framework frame sink.
void onWorldPassBeat();

// Keeps the producer target alive; `active` false skips the world pass.
void setActive(bool active);

// Selects the mask texture the external-texture draw applies (square/round).
void setMask(int textureId);
void forgetMask(int textureId);

// Orthographic zoom (the hud_radar_zoom cvar): the visible span is
// 2 * (4096 / zoom) world units.
void setZoom(float zoom);

// True when the FBO colour texture exists and is registered.
bool ready();

// Bumped whenever the FBO texture handle changes (creation / regeneration).
int generation();

} // namespace radarrender

} // namespace framework
