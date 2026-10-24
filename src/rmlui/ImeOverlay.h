#pragma once

// Platform IME state only. Candidate-window documents and styling belong to
// the module owning the focused text editor.
#include <abcui/module_api.h>
#include <string>

namespace rmlui::ime {
void shutdown();
void setComposition(const char *utf8, int cursorBytes);
void feedCandidatesFromIme(void *himc);
void setAnchor(float xPixels, float yPixels, float lineHeightPixels);
void hide();
std::string inputLanguageTag();
void snapshot(AbcUiImeState &out);
} // namespace rmlui::ime
