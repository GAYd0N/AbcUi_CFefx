#pragma once

// rmlui::TextInput - engine text events -> Rml editors, plus the IME
// composition adapter (T07).
//
// Event source (T07 step 1, evidence in execution/T07.md): the SDL2-based
// engine forwards raw SDL_Event structs to the BaseUI
// CallEngineSurfaceAppHandler seam (VGUI2Extension proxy, pre+post dispatch -
// text events are consumed at pre-dispatch only). The SDL fork delivers:
// - SDL_EVENT_TEXT_INPUT (0x303): committed text, NUL-terminated UTF-8 in a
//   32-byte buffer. The engine console eats its own copy behind a
//   console-mode gate; the original GameUI surface consumes the same events
//   for its TextEntry widgets (no SUPERCEDE here - the original UI stays
//   functional until T08).
// - SDL_EVENT_TEXT_EDITING (0x302): composition updates (text + byte-offset
//   cursor); an empty text during a composition is the cancel.
// - SDL_EVENT_TEXT_EDITING_CANDIDATES (0x304): candidate list updates.
//   Observed, not consumed: the candidate window itself stays platform
//   presented (T07 step 3), positioned through the SystemInterface
//   ActivateKeyboard seam the pin's WidgetTextInput already drives.
//
// There is no WM_CHAR / WM_IME_* path in this engine (the non-SDL branch of
// VGUI2Extension is dead code here), so chars are NEVER inferred from
// keynums: the key stream (T06 router) carries only control keys
// (navigation, backspace, enter, esc) and the shortcuts that Rml's
// WidgetTextInput resolves itself (Ctrl+C/X/V over the shared clipboard).
// One character source, no double submission.
//
// Composition tracking is scoped to editors of THIS context: the handler
// only activates when a Rml text widget of ours takes focus (RmlUi pin
// WidgetTextInput -> TextInputHandler::OnActivate). While the original
// GameUI composes into its own fields, no context of ours is active and
// every event below is ignored - half a pinyin string can never become a
// command.

// The engine include order drags Windows.h in before this header
// (metahook.h first), and winnt.h's `Assert` macro collides with RmlUi's
// own `Rml::Assert` declaration inside its headers - drop the macro before
// any Rml include, for the rest of the translation unit.
#ifdef Assert
#undef Assert
#endif

#include <RmlUi/Core/Header.h>
#include <RmlUi/Core/Traits.h>
#include <RmlUi/Core/TextInputHandler.h>
#include <RmlUi/Core/Types.h>

namespace rmlui {

// Production handler instance (also the composition state machine).
Rml::TextInputHandler &textInput();

// True while a composition is tracked for one of our focused editors.
bool textComposing();

// Committed text (SDL_EVENT_TEXT_INPUT payload, UTF-8). During a
// composition this is the commit; otherwise it is submitted to the focused
// editor directly (maxlength respected by the pin).
void feedTextInput(const char *utf8);

// T16 console-toggle seam: the backquote key both toggles the console and
// delivers an SDL text event ("`" / "~" depending on shift). The character
// belongs to the toggle, never to a focused editor (vanilla parity: the
// toggle key does not type). The console handler arms this at the toggle
// edge; the NEXT commit of "`"/"~" is dropped, and any other commit disarms
// the flag (so a later deliberate paste is unaffected).
void swallowToggleKeyText();

// Composition update (SDL_EVENT_TEXT_EDITING payload). `startBytes` is the
// byte offset of the cursor inside the composition string.
void feedTextEditing(const char *utf8, int startBytes);

// Drop the running composition: Rml-side purge plus a best-effort platform
// cancel (ImmNotifyIME) so the next keystroke starts clean. The ESC-first
// semantics, editor focus loss and teardown all land here.
void cancelComposition();

// Window focus-loss mirror (WM_KILLFOCUS path): state-only cleanup. The
// OS cancels the composition by itself when the window deactivates; no
// platform call is made from here.
void onWindowFocusLost();

// The pre-dispatch SDL event entry (VGUI2ExtensionImport AppProc forward).
// Text events are consumed at pre-dispatch only - the extension repeats
// every event post-dispatch (07 doc item 7).
void onSurfaceAppEvent(const void *sdlEvent);

// Surface WndProc seam: capture the real game window handle for the
// platform composition/candidate positioning (the engine exposes no HWND
// through the interfaces we bind).
void noteSurfaceWindow(const void *hwnd);

// IME placement seam (AbcSystemInterface::ActivateKeyboard): position the
// platform composition + candidate windows at the editing caret. The caret
// arrives in context pixels == surface client pixels (T06 coordinate
// basis); the pin's WidgetTextInput re-reports on every caret move, so
// window moves and scroll updates are covered while composing.
void updateImePlacement(Rml::Vector2f caretPosition, float lineHeight);

// While composing, the IME owns these keys: Enter commits, ESC cancels,
// Backspace edits the composition. Feeding them to the editor as well
// would commit AND insert a newline / close the window / delete behind
// the IME's back.
bool compositionOwnsKey(int keynum);

// Teardown (RmlUiModule::shutdown, after the router reset): drop cached
// TextInputContext pointers while the DOM still exists.
void textInputShutdown();

// On-machine diagnostics (V07 event-source evidence): counters for the
// text/IME inputs and the keyboard messages the surface observes.
void noteWndProcMessage(unsigned msg);
void eventCounters(unsigned &appProcTotal, unsigned &appProcTextInput,
                   unsigned &appProcTextEditing, unsigned &appProcCandidates,
                   unsigned &wndProcTotal, unsigned &wndProcChar,
                   unsigned &wndProcKey, unsigned &feedCommits,
                   unsigned &feedDrops);
void messageHistogram(unsigned *wndProcTypes, unsigned *appProcTypes);

} // namespace rmlui
