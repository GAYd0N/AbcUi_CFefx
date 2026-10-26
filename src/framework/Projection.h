#pragma once

// framework::projection - the world-to-screen helper the old utility/util.cpp
// VEC_WorldToScreen provided (ABCEnchance "sprite draw helpers stay UI domain"
// split, render backend contract header note). Screen-space output in Rml coordinates
// (top-left origin). Consumers: batch B Crosshair (third-person center),
// GIndicator, PopNum.

namespace framework {

namespace projection {

// Projects a world point; screen[0]/[1] receive the pixel position. The old
// clamps to >=0 stand (negative results only occur off-view; every consumer
// runs a front-facing dot check first). Returns the raw pTriAPI result
// (non-zero = point behind the camera). Plain float[3] (== the HLSDK vec3_t)
// keeps this header self-contained (09 §2).
int worldToScreen(const float world[3], float screen[2]);

} // namespace projection

} // namespace framework
