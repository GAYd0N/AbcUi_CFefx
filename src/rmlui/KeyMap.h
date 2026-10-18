#pragma once

// rmlui::KeyMap - engine keynums (HLSDK keydefs.h + lowercased ASCII) ->
// Rml::Input::KeyIdentifier. Plan 05 §3.5.
//
// RmlUi's KeyIdentifier follows Win32 VK codes; the GoldSrc numbering is
// different (K_TAB=9 by coincidence, K_ENTER=13, K_ESCAPE=27, arrows at
// 128+...), so a table maps the set the UI consumes.
//
// The reverse direction (SDL app-event keycode -> engine keynum) lives here
// too: the menu-owned keyboard stream (T09/T16) delivers raw SDL keycodes and
// must land in the same engine keynum space before it can join routeKey.

#include <RmlUi/Core/Input.h>

namespace rmlui {

// Unknown keynums map to KI_UNKNOWN.
Rml::Input::KeyIdentifier mapKeyCode(int keynum);

// SDL app-event keycode (the `key` field of the engine's SDL key records) ->
// engine keynum (keydefs.h), i.e. the space mapKeyCode consumes. Returns -1
// for keys neither the engine nor the UI consumes.
//
// Printable ASCII is the identity (the engine's own character-key convention,
// keydefs.h: "normal keys should be passed as lowercased ascii"); the SDL
// keycode carries the shifted character, so uppercase folds down to the
// lowercase engine keynum.
//
// Everything else arrives as an SDL keycode built from the SDL scancode
// (SDL_SCANCODE_TO_KEYCODE = scancode | 0x40000000, same scheme in SDL2 and
// the SDL3 fork this engine ships - thirdparty SDL3_fork SDL_keycode.h), so
// the non-printable half is a scancode table. Scancode numbering is stable
// across those SDL versions; the SDL 1.2-era numeric keycodes (273..276 and
// friends) never appear on this stream and are deliberately rejected.
int mapSdlKeycode(int keycode);

} // namespace rmlui
