#pragma once

// rmlui::ime - the IME composition and candidate overlay.
//
// The engine is SDL-based: the BaseUI app seam forwards SDL_EVENT_TEXT_EDITING
// (the running composition) and SDL_EVENT_TEXT_EDITING_CANDIDATES (the
// candidate page). The composition string already renders inline - RmlUi
// underlines the composition range inside the focused editor - but the
// candidate page has no renderer on this build: the client's candidate window
// belongs to vgui2 (IInput2, the interface CaptionMod's VGUI chat entry drives
// through vgui::input()), and vgui2 only presents it for its own panels. An
// Rml editor would therefore type pinyin and never get to pick a character.
//
// This module draws that half in RmlUi, in the HUD's language: one small
// panel at the caret whose header is the composition string and whose rows
// are the candidates, the selected row in amber. It is a plain document on
// the shared context (no HudPanel), so the HUD, the menus and the console all
// get it, it sits above every page tier and it never takes the pointer.
//
// Placement: Rml's SystemInterface::ActivateKeyboard hands the caret over in
// surface pixels - the same pixels the context is sized in - while the panel
// is positioned in dp, so the anchor is divided by the context's density
// ratio and clamped; near the bottom edge the panel flips above the caret.

#include <string>
#include <vector>

#include <RmlUi/Core/Types.h>

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

namespace rmlui {
namespace ime {

// Loads the overlay document (hidden). Context lifetime.
void init(Rml::Context *context);
// Drops the document while the context still exists.
void shutdown();

// SDL_EVENT_TEXT_EDITING: the running composition (`utf8`, NUL-terminated)
// and the caret's byte offset inside it. An empty composition ends it.
void setComposition(const char *utf8, int cursorBytes);

// The IME's candidate page (imm32 CANDIDATELIST; `himc` is an HIMC). Called
// while composing from the caret update: the list, the page window the IME is
// on and the selected row are copied out; an empty list closes the rows and
// leaves only the composition header.
void feedCandidatesFromIme(void *himc);

// SystemInterface::ActivateKeyboard: caret anchor in surface pixels.
void setAnchor(float xPixels, float yPixels, float lineHeightPixels);

// Commit, cancel or focus loss: the whole overlay goes away.
void hide();

// The active input method's short tag ("CN", "TW", "JP", "KR"), empty when
// the current keyboard layout is not an IME. Read from the game thread's
// keyboard layout (ImmIsIME), so switching IMEs updates the chat input's
// badge on the next frame.
std::string inputLanguageTag();

} // namespace ime
} // namespace rmlui
