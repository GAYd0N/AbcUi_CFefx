#pragma once

// vgui2ext - VGUI2Extension.dll import helper (R-4 source of truth).
// Mirrors Renderer's src/VGUI2ExtensionImport.cpp pattern: resolve the plugin
// module + factories once at LoadClient, expose only what AbcUi consumes.
//
// R-4 verdict (Phase 3): IVGUI2Extension.h lives in the pinned VGUI2Extension
// submodule (include/Interface/, API v010). Mouse capability: full WM-level
// events via IVGUI2Extension_BaseUICallbacks::CallEngineSurfaceWndProc, or
// polling via ISurface2 (SurfaceGetCursorPos/IsCursorVisible). Phase 3 polls;
// batch C re-evaluates the callback path (plan 05 §3.5).
//
// Everything degrades to null when VGUI2Extension.dll is absent: texture
// uploads and cursor polling then report unavailable (plan 05 §3.5 fallback:
// keyboard-only panels).

#include <string>

namespace vgui2ext {

// LoadClient time; safe to call when the plugin is missing.
void init();
void shutdown();
// T08 plugin-teardown-only counterpart of init(): unregisters the native-UI
// (GameUI/GameConsole) callbacks and drops the paint gate if it is still
// installed. NOT part of the HUD-cycle shutdown() above - the takeover has
// no registration gaps at map changes.
void shutdownNative();

// True when the ISurface2 interface was resolved.
bool available();

// Texture channel (ids live in the surface texture namespace that
// IMetaRenderer's DrawTexturedRect consumes). Returns 0 when unavailable.
int createTexture();
void uploadTexture(int textureId, const unsigned char *rgba, int width,
                   int height);
void deleteTextureById(int textureId);

// Cursor state for InputBridge (window/pixel coords, y-down). Returns false
// when unavailable.
bool getCursorPos(int &x, int &y);
bool isCursorVisible();

// Game language setting ("english"/"schinese"/...), empty when unavailable.
const char *currentLanguage();

// IGameUIFuncs::Key_NameForKey (engine factory) - the display name for an
// engine keynum, "" when unavailable (vote panel yes/no labels).
const char *keyNameForKey(int keynum);

// T11: IGameUIFuncs::Key_BindingForKey (vtable slot 3; hw.dll 0x01dbd2b0
// reads the same key table the `bind` command writes) - the command bound
// to a keynum, "" / null when unbound or unavailable.
const char *keyBindingForKey(int keynum);

// T11: IGameUIFuncs::Key_KeyStringToKeyNum (vtable slot 9) - the keynum for
// a key name, -1 for unknown / unavailable.
int keyStringToKeyNum(const char *name);

// T13: IGameUIFuncs::IsKeyDown (vtable slot 1, hw.dll 0x01dbd370) - the
// button-state query behind the original voice/mouse command toggles
// ("in_mlook"/"in_jlook"). false when the interface is missing or the
// engine rejects the name (fail-closed: the row then reads as off).
bool isKeyDown(const char *name);

// T14: the loading surfaces' password prompt. Sven's engine asks for a server
// password through IGameUI slot +0x58 (CGameUI::ShowServerPasswordDialog,
// GameUI.dll 0x10006c60) - a slot BEYOND the 19 methods the extension proxies,
// so it is observed through a dedicated VFTHook on the GameUI instance
// (installed with the paint gate, slot validated against the GameUI.dll image
// range - fail-closed skip on layout drift, never a guessed address). The
// borrowed string is copied into the loading feed; the vanilla dialog is not
// created (the replacement overlay owns the prompt).
bool gameUiLoadingHookInstalled();
void uninstallGameUiLoadingHook();

// T14: vgui::ILocalize003 from vgui2.dll (VGUI_Localize003, resolved through
// the vgui2 factory) - resolves the engine's own "#GameUI_..." tokens (the
// loading progress/status/failure strings the shipped dialog displays). ""
// when the token is unknown or the interface is missing - the caller keeps
// the raw text in that case (never a silently blanked line).
std::string localizeToken(const char *token);

namespace video {
struct Mode {
  int Width = 0;
  int Height = 0;
  int Bpp = 0;
};
} // namespace video

// T12: IGameUIFuncs::GetCurrentVideoMode / GetCurrentRenderer (slots 6/7;
// hw.dll 0x01dbd2f0/0x01dbd310) - the ACTUAL current mode plus the windowed
// flag. false when the engine interface is unavailable or the mode is
// unreadable.
bool currentVideoMode(int &width, int &height, int &bpp, bool &windowed);

// T12: IGameUIFuncs::GetVideoModes (slot 5; hw.dll 0x01dbd2d0) - copies up
// to `max` enumerated modes, returns the full enumerated count (0 when
// unavailable). The caller owns the buffer.
int videoModeList(video::Mode *out, int max);

// Selects and locks the native arrow without moving it. Owned by the
// router's exclusive edge, shared by menus and interactive HUD owners.
// Returns false when the surface interface is missing.
bool showCursor();
// Scoreboard's explicit pointer-mode entry additionally centers the cursor.
bool centerAndUnlockCursor();
// Releases the shape lock and restores VGUI visibility on the last owner exit.
void lockCursor();
// Drain SDL2's relative delta without changing the cursor or feeding gameplay.
void discardRelativeMouseMotion();

// R-4 batch C: true once the BaseUI callbacks (engine surface WndProc) are
// registered - InputBridge then drives the Rml mouse from window messages
// instead of the async-key poll (which cannot see injected input).
bool uiCallbacksRegistered();

// T08: true once the native-UI (GameUI/GameConsole/ClientVGUI) callbacks are
// registered - the entry-layer takeover: menu show/hide answers, ESC routing,
// console suppression, paint-gate teardown. Engine-session scoped: registered
// once at the first init() and removed only by shutdownNative().
bool nativeCallbacksRegistered();

// T09: the proven BaseUI-equivalent resume path. The vanilla ResumeGame
// command falls through to IBaseUI::HideGameUI (GameUI.dll 0x1000cc30 ->
// vtable slot 0x1c), so the replacement does the same: the call runs through
// the engine interface the extension has VFT-hooked, which keeps the outer
// CBaseUI engine-root side effects while the suppressed inner dispatch sees
// the takeover's answers. Returns false only when the engine interface is
// unavailable (logged once; the callers still lower their own state).
bool hideGameUi();

} // namespace vgui2ext
