#pragma once

// framework::CurrentLanguage - the engine UI language name (e.g. "english").
// Implemented by the VGUI2 import in src/rmlui/LanguageChannel.cpp so the
// framework layer does not include the RmlUi/VGUI2 adapter.

namespace framework {

const char *CurrentLanguage();

} // namespace framework
