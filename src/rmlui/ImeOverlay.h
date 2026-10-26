#pragma once

// Platform IME state only. Candidate-window documents and styling belong to
// the module owning the focused text editor.
#include <abcui/module_api.h>
#include <string>

namespace rmlui::ime {
void shutdown();
void setComposition(const char *utf8, int cursorBytes);
void feedCandidatesFromIme(void *himc);
// Candidate page straight from the SDL candidates event payload (the TSF
// driven IMEs ship the strings in the event; the IMM32 CANDIDATELIST stays
// empty). Null candidates close the page.
void feedCandidatesFromEvent(const char *const *candidates, int count,
                             int selected);
// Raw payload capture (abc_ui_input_events layout dump): the first words of
// every candidates event, taken before any parsing decides anything.
void noteCandidatesPayload(const unsigned *words);
void clearCandidates();
void setAnchor(float xPixels, float yPixels, float lineHeightPixels);
void hide();
std::string inputLanguageTag();
void snapshot(AbcUiImeState &out);
// abc_ui_input_events diagnostics: candidate-read statistics. lastBytes is
// the last IMM32 read size (0 = the context carried no list); eventCount is
// the last event payload size (-1 = the payload was not usable).
void imeDebug(unsigned &feeds, unsigned &lastBytes, unsigned &eventFeeds,
             int &lastEventCount, int &count, int &selected, bool &anchorValid,
             int &compositionBytes);
// Raw candidates-event dump (abc_ui_input_events): the first words of the
// last payload seen, so the live layout is identified on-machine instead of
// guessed. `words` receives 8 entries.
void imePayloadDebug(unsigned &seen, unsigned *words);
} // namespace rmlui::ime
