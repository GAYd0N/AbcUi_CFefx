// framework::projection implementation - see Projection.h.
#include <metahook.h>

#include "Projection.h"

#include <triangleapi.h>

#include <algorithm>

#include "ScreenMetrics.h"

// Windows.h min/max macros fight std::max (old repo precedent).
#undef max
#undef min

namespace framework {

namespace projection {

int worldToScreen(const float world[3], float screen[2]) {
  int width = 1920, height = 1080;
  framework::ScreenSize(width, height);
  // pTriAPI writes x/y/z; z lands past the caller's pair.
  float raw[3] = {};
  const int result =
      gEngfuncs.pTriAPI->WorldToScreen(const_cast<float *>(world), raw);
  // Old VEC_WorldToScreen parity (NDC -> screen, y flip).
  screen[0] = (1.0f + raw[0]) * width / 2.0f;
  screen[1] = (1.0f - raw[1]) * height / 2.0f;
  return result;
}

} // namespace projection

} // namespace framework
