#pragma once

// vgui2ext::NativeEntryCallbacks - the production entry-layer callback objects
// (T08) split into their own translation unit so the exact decision/return
// logic links into offline tests (T21a: pre/post contract + coexistence with
// other extension plugins). The engine-coupled install/teardown seams
// (VGUI2ExtensionImport.cpp) stay behind injectable function pointers; the
// classes themselves only touch the NativeUiBridge, the loading feed, the
// console capture and those seams.
//
// Result discipline (hook-contract §3/T1/T2, re-verified in T21a):
//   - pre runs the ONE business action, writes the plugin return value
//     through pPluginReturnValue with the REAL type (int slots via int*,
//     bool slots via bool* - never a pointer swap) and sets SUPERCEDE to
//     skip the original;
//   - the proxy resets Result between phases, so the post phase re-writes
//     the value and sets OVERRIDE (call real, use my value - the value
//     claim without a second "skip"): a post left UNSET silently falls back
//     to the original's return value;
//   - void callbacks (HideGameUI, GameConsole Activate/Hide) keep SUPERCEDE
//     in both phases; HideGameUI cannot use IsPost at all (the wrapper sets
//     IsPost=false in both dispatches, hook-contract T5) - the bridge's
//     per-press latch coalesces the duplicate dispatch;
//   - callbacks this plugin only observes leave Result UNSET.

#include <IVGUI2Extension.h>

namespace vgui2ext {

// Engine-coupled seams; production wires them in VGUI2ExtensionImport.cpp
// before the callbacks are registered, tests wire recording fakes. All are
// optional: a null seam degrades to a no-op (never a crash).
struct NativeEntrySeams {
  // One synthetic ESC press through the input router (route-close path of
  // HideGameUI, T07 semantics: composition cancel first, close otherwise).
  void (*routeEscPress)(void) = nullptr;
  // True while some owner holds UI input (router exclusivity) - the
  // route-close path falls back to a state-only close without it.
  bool (*menuOwned)(void) = nullptr;
  // The console toggle key's committed character swallow (T16).
  void (*swallowToggleKeyText)(void) = nullptr;
  // GameUI::Start notification: install the password-prompt slot hook.
  void (*installPasswordPromptHook)(void) = nullptr;
  // Teardown: drop the IPanel paint gate (GameUI::Shutdown fallback) and
  // the password-prompt hook (ClientVGUI Shutdown, GameUI still mapped).
  void (*detachPaintGate)(void) = nullptr;
  void (*detachPasswordPromptHook)(void) = nullptr;
  // Disconnect is a session end, not an engine/plugin shutdown.
  void (*onDisconnected)(void) = nullptr;
};

void setNativeEntrySeams(const NativeEntrySeams &seams);

// The singleton callback instances registered with the extension.
IVGUI2Extension_GameUICallbacks &gameUICallbacks();
IVGUI2Extension_GameConsoleCallbacks &gameConsoleCallbacks();
IVGUI2Extension_ClientVGUICallbacks &clientVGUICallbacks();

} // namespace vgui2ext
