#pragma once

#include <abcui/module_api.h>

namespace framework::TexturesApi {
const AbcUiTexturesApi *get();
// Caller-owned texture, or zero for a stale/foreign handle.
int textureId(AbcUiTexture texture);
} // namespace framework::TexturesApi
