#pragma once

#include <abcui/module_api.h>

namespace framework::AudioApi {
const AbcUiAudioApi *get();
void pump();
} // namespace framework::AudioApi
