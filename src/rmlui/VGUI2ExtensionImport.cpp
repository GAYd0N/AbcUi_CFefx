// vgui2ext implementation - see VGUI2ExtensionImport.h.
#include <metahook.h>

#include "VGUI2ExtensionImport.h"

// RmlUi headers first: the SourceSDK mathlib chain the VGUI/engine interface
// headers pull in defines Assert/FloatEqual as function-like macros
// (tier0/dbg.h), which would break RmlUi's own Debug.h macros if it were
// parsed afterwards. vgui2 extension code that needs both keeps this order.
#include "InputBridge.h"
#include "NativeEntryCallbacks.h"
#include "RmlUiModule.h"
#include "TextInput.h"

#include <ISurface2.h>
#include <vgui/Cursor.h>
#include <IVGUI2Extension.h>
#include <IGameUIFuncs.h>
#include <ILocalize.h>
#include <IBaseUI.h>
#include <keydefs.h>

#include <abc/Console.h>
#include <abc/CvarCmd.h>
#include <abc/Plugin.h>

#include "PaintGate.h"
#include "../services/GameUiBridge.h"
#include "../services/console/ConsoleBackend.h"
#include "../services/console/ConsoleBuffer.h"

namespace vgui2ext {

namespace {

IVGUI2Extension *Extension = nullptr;
// T08: the native-UI callbacks (GameUI/GameConsole) are registered once and
// stay registered for the whole engine session - the takeover must not have
// registration gaps at map changes, so they are NOT tied to the HUD-cycle
// scoped shutdown() above. shutdownNative() removes them at plugin teardown.
IVGUI2Extension *NativeExtension = nullptr;
vgui::ISurface2 *Surface2 = nullptr;
IGameUIFuncs *GameUIFuncs = nullptr;
bool WarnedUnavailable = false;
bool UICallbacksRegistered = false;
bool NativeCallbacksRegistered = false;

bool warnUnavailableOnce() {
  if (!WarnedUnavailable) {
    abc::ConsoleWarn("[AbcUi] VGUI2Extension.dll not found - surface textures "
                     "and mouse polling disabled\n");
    WarnedUnavailable = true;
  }
  return false;
}

// SDL key-event view for the physical ESC release AND the T16 no-map
// console close (SDL3-style ids, SDL2-style layout - same evidence as
// TextInput.cpp's text views). Observation only: the extension's own
// handler owns real dispatch; this seam never writes Result.
constexpr unsigned kSdlEventKeyDown = 0x300;
constexpr unsigned kSdlEventKeyUp = 0x301;
constexpr int kSdlKeycodeEscape = 0x1B;

struct SdlKeyEvent {
  unsigned Type;
  unsigned Timestamp;
  unsigned WindowId;
  unsigned Which;
  int Scancode;
  int Key;
  unsigned short Mod;
  unsigned short Raw;
  bool Down;
  bool Repeat;
};

void notePhysicalEscUpFromEvent(const void *event) {
  const auto *key = static_cast<const SdlKeyEvent *>(event);
  if (key->Type != kSdlEventKeyUp || key->Key != kSdlKeycodeEscape)
    return;
  services::nativeUiBridge().noteEscPhysicalUp();
}

// T16 close route for the no-map console: with no level loaded the engine's
// window handler turns ESC into ActivateGameUI (a no-op once the menu is
// up), so the single routed close that serves the in-game case never
// happens. This observation (pre-dispatch, Result untouched - the engine
// keeps consuming ESC itself) covers exactly that gap: menu up, console
// presented, no level, one close per physical press (the shared
// EscPressRouted latch, cleared by the key-up above and at every raise).
void notePhysicalEscDownFromEvent(const void *event) {
  const auto *key = static_cast<const SdlKeyEvent *>(event);
  if (key->Type != kSdlEventKeyDown || key->Key != kSdlKeycodeEscape ||
      key->Repeat)
    return;
  auto &bridge = services::nativeUiBridge();
  if (!bridge.armed() || !bridge.menuUp() || !bridge.consoleRequested() ||
      bridge.hasLevel())
    return;
  if (!bridge.consumeEscRoute())
    return;
  bridge.requestConsoleClose(services::NativeUiOrigin::kGameConsole);
}

// R-4 callback path (batch C): the engine surface WndProc sees every UI-level
// mouse message - including injected ones the async-key poll cannot observe
// (GetAsyncKeyState never sees SDL/window-message input). Observation-only:
// the result stays IGNORED, the vanilla surface still handles everything.
class UICallbacks final : public IVGUI2Extension_BaseUICallbacks {
public:
  int GetAltitude() const override {
    return 0;
  }
  void Initialize([[maybe_unused]] CreateInterfaceFn *factories,
                  [[maybe_unused]] int count) override {
    // T08 step 1: the earliest proven install point (T02 §5: ~1.3s
    // before the first native paint). The gate arms itself only after
    // the IPanel hooks are verified in place; an unsupportable binary
    // stops here (SysError) instead of running with the old UI.
    rmlui::installPaintGate();
  }
  void Start([[maybe_unused]] struct cl_enginefuncs_s *engineFuncs,
             [[maybe_unused]] int interfaceVersion) override {}
  void Shutdown() override {
    // Deliberately empty: at BaseUI::Shutdown vgui2 is already gone
    // (hook-contract §9). The IPanel hooks come down in the GameUI
    // Shutdown notification instead.
  }
  void Key_Event(int &down, int &keynum,
                 [[maybe_unused]] const char *&pszCurrentBinding,
                 VGUI2Extension_CallbackContext *CallbackContext) override {
    // This is the seam through which the engine asks the UI layer whether it
    // consumed a key (CBaseUI::Key_Event). It is how the native chat entry
    // and CaptionMod's VGUI chat dialog keep the player still: a focused
    // text entry reports the key handled, and hw.dll's Key_Event then skips
    // the binding outright - no usercmd filtering involved. AbcUi has no
    // VGUI entry, so while the chat input line owns the keyboard
    // (Owner::blocksGameplay) the same answer is produced here: the edge goes
    // through the one injection point and the callback reports it handled.
    //
    // Edges the owner declines (the router's pre-held passthrough, so bindings
    // pair cleanly) are left to the engine: no SUPERCEDE, no synthetic
    // release. rmlui::routeKey de-duplicates the two seams that can deliver
    // one physical edge, so the editor never sees a doubled keystroke.
    services::nativeUiBridge().noteBaseUiKeyEvent();
    if (CallbackContext->IsPost)
      return;
    if (!rmlui::uiRouter().blocksGameplay())
      return;
    if (!rmlui::routeKey(down != 0, keynum, false))
      return;
    CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
    CallbackContext->pPluginReturnValue = reinterpret_cast<void *>(1);
  }
  // T07 text seam: the engine forwards raw SDL_Event structs here (the
  // SDL2-based IBaseUI shape - CallEngineSurfaceAppHandler). Text events
  // are consumed at PRE-dispatch only: the proxy repeats every event
  // post-dispatch (07 doc item 7) and a second feed would insert the
  // committed text twice. Observation-only otherwise - the Result is never
  // written, the original surface keeps its own copy of every event.
  void CallEngineSurfaceAppProc(
      void *&pevent, [[maybe_unused]] void *&userData,
      VGUI2Extension_CallbackContext *CallbackContext) override {
    if (!CallbackContext->IsPost) {
      // Physical ESC release tracking (one close route per press);
      // T16: the ESC-down path closes the no-map console (see the
      // helper) - both observation-only, Result untouched.
      notePhysicalEscUpFromEvent(pevent);
      notePhysicalEscDownFromEvent(pevent);
      rmlui::onSurfaceAppEvent(pevent);
      // T09: the menu-owned keyboard stream - with the takeover active
      // the engine stops feeding HUD_Key_Event, so the surface SDL
      // keys/wheel are the only physical source left; they join the
      // same routeKey injection point (ESC excluded there).
      rmlui::onSurfaceMenuKey(pevent);
    }
  }
  void CallEngineSurfaceWndProc(void *&hwnd, unsigned int &msg,
                                unsigned int &wparam, long &lparam,
                                [[maybe_unused]] VGUI2Extension_CallbackContext
                                    *CallbackContext) override {
    rmlui::onSurfaceWndProc(hwnd, msg, wparam, lparam);
  }
  void Paint([[maybe_unused]] int &x, [[maybe_unused]] int &y,
             [[maybe_unused]] int &right, [[maybe_unused]] int &bottom,
             VGUI2Extension_CallbackContext *CallbackContext) override {
    if (CallbackContext->IsPost)
      rmlui::maintainUiMouse();
  }
  void HideGameUI(VGUI2Extension_CallbackContext *CallbackContext) override {
    // The outer (engine CBaseUI) layer stays native: its engine-root
    // flips are the necessary side effects the takeover preserves. The
    // inner IGameUI layer is where the suppression happens; this callback
    // is observation only.
    if (CallbackContext->IsPost)
      rmlui::maintainUiMouse();
    services::nativeUiBridge().noteBaseUiToggle();
  }
  void
  ActivateGameUI(VGUI2Extension_CallbackContext *CallbackContext) override {
    if (CallbackContext->IsPost)
      rmlui::maintainUiMouse();
    services::nativeUiBridge().noteBaseUiToggle();
  }
  void HideConsole(VGUI2Extension_CallbackContext *CallbackContext) override {
    // T16: the outer Show/HideConsole bodies only flip the engine's own
    // console-toggle flag and forward to IGameConsole, so the native side
    // stays (engine flag parity) and the inner GameConsole callbacks own
    // the takeover. Observation only here.
    if (CallbackContext->IsPost)
      rmlui::maintainUiMouse();
  }
  void ShowConsole(VGUI2Extension_CallbackContext *CallbackContext) override {
    if (CallbackContext->IsPost)
      rmlui::maintainUiMouse();
  }
};

UICallbacks UICallbacksInstance;

// T14 (defined below with its install/uninstall seams): the GameUI::Start
// notification installs the Sven password-prompt slot hook; the ClientVGUI
// Shutdown seam removes it. The three entry-layer callback classes moved to
// NativeEntryCallbacks.cpp (T21a) and reach this TU through NativeEntrySeams.
void installPasswordPromptHook();

// The Sven-only password prompt extends IGameUI beyond the methods proxied
// by VGUI2Extension. Its verified slot is supplied by binary-specific gamedata.
// Unknown binaries are skipped, never assigned a guessed interface slot.
IGameUI *PasswordPromptGameUi = nullptr;
hook_t *PasswordPromptHook = nullptr;
// ShowServerPasswordDialog(this, serverName, flag): zero means invalid
// password.
using ShowServerPasswordFn = void(__fastcall *)(void *, int, const char *, int);

void __fastcall Hook_ShowServerPasswordDialog([[maybe_unused]] void *pthis,
                                              [[maybe_unused]] int dummy,
                                              const char *serverName,
                                              int flag) {
  services::LoadingFeedEvent event;
  event.kind = services::LoadingFeedKind::kPasswordPrompt;
  event.text = serverName ? serverName : "";
  event.error = flag == 0; // 0 = the previous password was rejected
  services::loadingFeed().push(event);
  // No original call: the vanilla dialog is out of the takeover's scope and
  // its creation is not provably gated (see the note above).
}

// Installed at GameUI::Start, after GameUI.dll and its live vtable exist.
void installPasswordPromptHook() {
  if (PasswordPromptHook)
    return;
  const HMODULE gameUi = GetModuleHandleA("GameUI.dll");
  if (!gameUi) {
    abc::ConsoleWarn("[AbcUi] password prompt hook: GameUI.dll not loaded\n");
    return;
  }
  const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(gameUi);
  const auto nt =
      dos->e_magic == IMAGE_DOS_SIGNATURE
          ? reinterpret_cast<const IMAGE_NT_HEADERS *>(
                reinterpret_cast<const BYTE *>(gameUi) + dos->e_lfanew)
          : nullptr;
  if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) {
    abc::ConsoleWarn(
        "[AbcUi] password prompt hook: GameUI.dll image header invalid\n");
    return;
  }
  const auto imageBase = reinterpret_cast<const BYTE *>(gameUi);
  const auto imageEnd = imageBase + nt->OptionalHeader.SizeOfImage;
  const auto factory =
      Sys_GetFactory(reinterpret_cast<HINTERFACEMODULE>(gameUi));
  if (!factory) {
    abc::ConsoleWarn(
        "[AbcUi] password prompt hook: GameUI.dll has no factory\n");
    return;
  }
  auto *gameUiInstance =
      static_cast<IGameUI *>(factory(GAMEUI_INTERFACE_VERSION, nullptr));
  if (!gameUiInstance) {
    abc::ConsoleWarn("[AbcUi] password prompt hook: \"%s\" unavailable\n",
                     GAMEUI_INTERFACE_VERSION);
    return;
  }
  if (!g_pMetaHookAPI) {
    abc::ConsoleWarn(
        "[AbcUi] password prompt hook: metahook API unavailable\n");
    return;
  }
  uint32_t promptIndex = 0;
  const auto status = g_pMetaHookAPI->QueryGameSymbolScalar(
      gameUi, "AbcUi_CGameUI_ShowServerPasswordDialog_index", &promptIndex);
  if (status != MH_GAMESYMBOL_OK || promptIndex > 255) {
    abc::ConsoleWarn("[AbcUi] password prompt hook: gamedata unavailable or "
                     "invalid (%s), skipping\n",
                     g_pMetaHookAPI->GetGameSymbolStatusString(status));
    return;
  }
  void **vtable = *reinterpret_cast<void ***>(gameUiInstance);
  const void *promptEntry = vtable[promptIndex];
  if (promptEntry < imageBase || promptEntry >= imageEnd) {
    abc::ConsoleWarn(
        "[AbcUi] password prompt hook: IGameUI password prompt slot (%p) "
        "outside the GameUI.dll image; layout drifted, skipping\n",
        promptEntry);
    return;
  }
  void *original = nullptr;
  hook_t *hook = g_pMetaHookAPI->VFTHook(
      gameUiInstance, 0, promptIndex,
      reinterpret_cast<void *>(&Hook_ShowServerPasswordDialog), &original);
  if (!hook) {
    abc::ConsoleWarn(
        "[AbcUi] password prompt hook: VFTHook gamedata slot failed\n");
    return;
  }
  PasswordPromptGameUi = gameUiInstance;
  PasswordPromptHook = hook;
  abc::ConsolePrint("[AbcUi] password prompt hook installed (IGameUI password "
                    "prompt slot -> 0x%p)\n",
                    promptEntry);
}

// --- T15: console backend production seams ----------------------------------
// The user submission channel (services::console::ConsoleBackend) executes
// through pfnClientCmd - the same engine command buffer the shipped console
// entry submitted to; the engine's own command/alias chain consumes the line
// exactly once. The enumeration seams read the REAL runtime catalogs with the
// engine's own cursors (hw.dll exports, the ABCEnchance/Find precedent):
// cvars via the GetFirstCvarPtr chain, commands via the
// GetFirstCmdFunctionHandle cursor. Every name is copied by the caller before
// the next engine call; nothing outlives the scan.

std::size_t enumerateCvars(const char **out, std::size_t max) {
  if (!out || max == 0 || !gEngfuncs.GetFirstCvarPtr)
    return 0;
  std::size_t count = 0;
  for (cvar_t *cvar = gEngfuncs.GetFirstCvarPtr(); cvar && count < max;
       cvar = cvar->next) {
    if (cvar->name && *cvar->name)
      out[count++] = cvar->name;
  }
  return count;
}

std::size_t enumerateCommands(const char **out, std::size_t max) {
  if (!out || max == 0 || !gEngfuncs.GetFirstCmdFunctionHandle ||
      !gEngfuncs.GetNextCmdFunctionHandle || !gEngfuncs.GetCmdFunctionName)
    return 0;
  std::size_t count = 0;
  for (unsigned int handle = gEngfuncs.GetFirstCmdFunctionHandle();
       handle && count < max;
       handle = gEngfuncs.GetNextCmdFunctionHandle(handle)) {
    const char *name = gEngfuncs.GetCmdFunctionName(handle);
    if (name && *name)
      out[count++] = name;
  }
  return count;
}

void wireConsoleBackend() {
  services::console::ConsoleBackend::Seams seams;
  seams.clientCmd = [](const char *command) {
    if (!command)
      return false;
    abc::EngineClientCmd(command);
    return true; // handed to the engine command buffer (flush between frames)
  };
  seams.listCvars = &enumerateCvars;
  seams.listCommands = &enumerateCommands;
  services::console::consoleBackend().setSeams(seams);
}

// The engine IBaseUI singleton (T09): resolved from the same factory the
// extension's BaseUI_Install used, so the vtable the pointer carries is the
// hooked (proxied) one. The outer CBaseUI bodies then keep their engine-root
// side effects while the inner takeover answers the queries. A failed
// resolution is retried by the next caller (the engine side may not be up).
IBaseUI *baseUi() {
  static IBaseUI *BaseUi = nullptr;
  if (BaseUi)
    return BaseUi;
  const auto factory =
      g_pMetaHookAPI ? g_pMetaHookAPI->GetEngineFactory() : nullptr;
  BaseUi =
      factory ? (IBaseUI *)factory(BASEUI_INTERFACE_VERSION, nullptr) : nullptr;
  return BaseUi;
}

} // namespace

// T21a: wire the entry-layer seams before any callback can fire. The callback
// classes live in NativeEntryCallbacks.cpp; these are their only handles on
// this TU's engine-coupled surfaces (routing, hook teardown).
void wireNativeEntrySeams() {
  NativeEntrySeams seams;
  seams.routeEscPress = []() {
    // One synthetic ESC press through the input router (T07 semantics:
    // composition cancel first, close otherwise).
    rmlui::routeKey(1, K_ESCAPE, false);
    rmlui::routeKey(0, K_ESCAPE, false);
  };
  seams.menuOwned = []() { return rmlui::uiRouter().exclusive(); };
  seams.swallowToggleKeyText = []() { rmlui::swallowToggleKeyText(); };
  seams.installPasswordPromptHook = []() { installPasswordPromptHook(); };
  seams.detachPaintGate = []() { rmlui::uninstallPaintGate(); };
  seams.detachPasswordPromptHook = []() { uninstallGameUiLoadingHook(); };
  seams.onDisconnected = []() { rmlui::onDisconnected(); };
  setNativeEntrySeams(seams);
}

void init() {
  const auto module = GetModuleHandleA("VGUI2Extension.dll");
  if (!module)
    return;
  const auto factory = Sys_GetFactory((HINTERFACEMODULE)module);
  if (!factory) {
    abc::SysError("[AbcUi] Could not get factory from VGUI2Extension");
    return;
  }
  Extension =
      (decltype(Extension))factory(VGUI2_EXTENSION_INTERFACE_VERSION, nullptr);
  if (!Extension) {
    abc::SysError("[AbcUi] Could not get interface \"%s\" from VGUI2Extension",
                  VGUI2_EXTENSION_INTERFACE_VERSION);
    return;
  }
  Surface2 =
      (decltype(Surface2))factory(VGUI_SURFACE2_INTERFACE_VERSION, nullptr);
  if (!Surface2) {
    abc::SysError("[AbcUi] Could not get interface \"%s\" from VGUI2Extension",
                  VGUI_SURFACE2_INTERFACE_VERSION);
    return;
  }
  // The key-name table lives in the engine (old VGUI2 BaseUI binding).
  GameUIFuncs = (decltype(GameUIFuncs))g_pMetaHookAPI->GetEngineFactory()(
      VENGINE_GAMEUIFUNCS_VERSION, nullptr);

  // R-4 batch C upgrade: WndProc callbacks drive the Rml mouse (clicks and
  // motion) - the async-key poll cannot see injected or message-level input.
  if (!UICallbacksRegistered) {
    Extension->RegisterBaseUICallbacks(&UICallbacksInstance);
    UICallbacksRegistered = true;
  }
  // T08: the native-UI takeover callbacks are engine-session scoped (see
  // the NativeExtension note above). Registration happens here, before
  // BaseUI::Initialize (T02 timeline: LoadClient -> registration ->
  // Initialize), so the cold-start GameUI activation already funnels
  // through them. The T15 console capture rides the same registration: the
  // earliest point this plugin can receive engine output (the cache is
  // usable before any Rml context exists).
  if (!NativeCallbacksRegistered) {
    wireNativeEntrySeams();
    Extension->RegisterGameUICallbacks(&gameUICallbacks());
    Extension->RegisterGameConsoleCallbacks(&gameConsoleCallbacks());
    // The teardown seam: ClientVGUI Shutdown fires first inside the
    // IBaseUI::Shutdown proxy, while vgui2 is still mapped.
    Extension->RegisterClientVGUICallbacks(&clientVGUICallbacks());
    NativeExtension = Extension;
    NativeCallbacksRegistered = true;
  }
  // T15: the submission/enumeration seams of the console backend.
  wireConsoleBackend();

  // T16 startup console entry (-console / -toconsole): the engine's own
  // startup call (native CBaseUI::Initialize body: CheckParm("-console")
  // || CheckParm("-toconsole") -> GameConsole::Activate and the
  // DAT_0204d7ec flag) runs BEFORE the extension installs the IGameConsole
  // proxies, so that call can never reach the callbacks - live-verified
  // (requested=0 after a -console cold start). The same engine-truthful
  // check latches the intent here (LoadClient, before the arm); the bridge
  // delivers it exactly once at arm() and the replacement console
  // presents on the first context instead of the legacy dialog.
  if (gEngfuncs.CheckParm && (gEngfuncs.CheckParm("-console", nullptr) ||
                              gEngfuncs.CheckParm("-toconsole", nullptr)))
    services::nativeUiBridge().requestConsoleOpen(
        services::NativeUiOrigin::kGameConsole);
}

void shutdown() {
  // HUD-cycle scoped: this runs at every HUD_Shutdown (map change) and is
  // re-armed by the next init(). The T08 native callbacks and the IPanel
  // gate deliberately survive these cycles - the takeover has no gaps.
  if (Extension && UICallbacksRegistered) {
    Extension->UnregisterBaseUICallbacks(&UICallbacksInstance);
    UICallbacksRegistered = false;
  }
  GameUIFuncs = nullptr;
  Surface2 = nullptr;
  Extension = nullptr;
}

void shutdownNative() {
  // Plugin teardown only (plugins.cpp OnShutdown). Fallback gate removal
  // for paths that never reached the GameUI::Shutdown notification.
  // T15 teardown order (strict): stop the console producers first (every
  // later callback is a no-op), then unregister the callbacks, and only
  // then drop the cache - no producer may push into a cache being cleared.
  services::console::consoleCapture().detach();
  rmlui::uninstallPaintGate();
  if (NativeExtension && NativeCallbacksRegistered) {
    NativeExtension->UnregisterGameUICallbacks(&gameUICallbacks());
    NativeExtension->UnregisterGameConsoleCallbacks(&gameConsoleCallbacks());
    NativeExtension->UnregisterClientVGUICallbacks(&clientVGUICallbacks());
    NativeCallbacksRegistered = false;
  }
  NativeExtension = nullptr;
  services::nativeUiBridge().setMenuSink(nullptr);
  services::console::consoleBuffer().shutdown();
}

bool uiCallbacksRegistered() {
  return UICallbacksRegistered;
}

bool nativeCallbacksRegistered() {
  return NativeCallbacksRegistered;
}

bool gameUiLoadingHookInstalled() {
  return PasswordPromptHook != nullptr;
}

void uninstallGameUiLoadingHook() {
  if (PasswordPromptHook) {
    if (g_pMetaHookAPI)
      g_pMetaHookAPI->UnHook(PasswordPromptHook);
    PasswordPromptHook = nullptr;
  }
  PasswordPromptGameUi = nullptr;
}

bool available() {
  return Surface2 != nullptr;
}

int createTexture() {
  if (!Surface2)
    return warnUnavailableOnce(), 0;
  return Surface2->CreateNewTextureID(true);
}

void uploadTexture(int textureId, const unsigned char *rgba, int width,
                   int height) {
  if (!Surface2 || textureId == 0)
    return;
  // hardwareFilter=1: linear filtering (UI glyphs/images), forceReload=false.
  Surface2->DrawSetTextureRGBA(textureId, rgba, width, height, 1, false);
}

void deleteTextureById(int textureId) {
  if (!Surface2 || textureId == 0)
    return;
  Surface2->DeleteTextureByID(textureId);
}

bool getCursorPos(int &x, int &y) {
  if (!Surface2)
    return warnUnavailableOnce(), false;
  Surface2->SurfaceGetCursorPos(x, y);
  return true;
}

bool isCursorVisible() {
  if (!Surface2)
    return false;
  return Surface2->IsCursorVisible();
}

const char *currentLanguage() {
  if (!Extension)
    return "";
  return Extension->GetCurrentLanguage();
}

const char *keyNameForKey(int keynum) {
  if (!GameUIFuncs)
    return "";
  return GameUIFuncs->Key_NameForKey(keynum);
}

const char *keyBindingForKey(int keynum) {
  if (!GameUIFuncs)
    return "";
  return GameUIFuncs->Key_BindingForKey(keynum);
}

int keyStringToKeyNum(const char *name) {
  if (!GameUIFuncs || !name)
    return -1;
  return GameUIFuncs->Key_KeyStringToKeyNum(name);
}

bool isKeyDown(const char *name) {
  if (!GameUIFuncs || !name)
    return false;
  bool down = false;
  // IGameUIFuncs slot 1 (hw.dll 0x01dbd370, T12 vtable pin): returns
  // success and writes the state; only a true+true pair counts as down.
  if (!GameUIFuncs->IsKeyDown(name, down))
    return false;
  return down;
}

std::string localizeToken(const char *token) {
  if (!token || !*token)
    return {};
  // vgui2.dll's localize implementation (VGUI_Localize003 - GameUI.dll
  // resolves its own "#GameUI_..." strings through it). Resolved lazily and
  // cached: the interface instance is process-scoped like the surface.
  static vgui::ILocalize *Localize = nullptr;
  static bool Resolved = false;
  if (!Resolved) {
    Resolved = true;
    const HMODULE vgui2 = GetModuleHandleA("vgui2.dll");
    if (vgui2) {
      const auto factory =
          Sys_GetFactory(reinterpret_cast<HINTERFACEMODULE>(vgui2));
      if (factory)
        Localize = static_cast<vgui::ILocalize *>(
            factory(VGUI_LOCALIZE_INTERFACE_VERSION, nullptr));
    }
  }
  if (!Localize)
    return {};
  const wchar_t *wide = Localize->Find(token);
  if (!wide || !*wide)
    return {};
  // UTF-16 -> UTF-8 (the engine localize stores UTF-16); bounded by the
  // interface's own conversion so the codepage semantics match the shipped
  // GameUI's display path.
  char buffer[1024] = {};
  if (Localize->ConvertUnicodeToANSI(wide, buffer, sizeof(buffer)) <= 0)
    return {};
  return buffer;
}

// The engine's vmode_s is 3 x int32 {width, height, bpp} (hw.dll enumeration
// array stride 0xc, pinned in the T12 execution record); the MetaHook header
// only forward-declares the type, so the view is local.
struct EngineVideoMode {
  int Width;
  int Height;
  int Bpp;
};

// Engine-truthful mirror of the IGameUIFuncs vtable (T12 ABI trap): the
// MetaHook header declares GetCurrentRenderer with THREE outputs, but the
// shipped engine implementation (hw.dll 0x01dbddb0, reached through the
// vtable slot at 0x01dbd310) reads SIX argument slots - (name, namelen,
// windowed, hdmodels, addons, vid_level), each nullptr-safe - and the shipped
// GameUI calls it with six arguments (GameUI.dll FUN_1001cca0 calls slot
// +0x1c with 6 args; the adjacent +0x18 slot with 3 for
// GetCurrentVideoMode). Calling through the 3-arg declaration leaves the
// last three stack slots undefined; the callee then writes through the
// garbage "pointers" (live-caught: engine assert dump with the stack inside
// FUN_01dbddb0 on the first settings-page open).
//
// The mirror keeps the declaration order (and therefore every vtable slot)
// of IGameUIFuncs - the base-interface destructor stays the first entry,
// which is what puts Key_NameForKey at index 2 / GetCurrentRenderer at
// index 7, the same indices T11 pinned. Only this one signature differs.
class EngineGameUIFuncs {
public:
  virtual ~EngineGameUIFuncs() = default;
  virtual bool IsKeyDown(const char *keyname, bool &isdown) = 0;
  virtual const char *Key_NameForKey(int keynum) = 0;
  virtual const char *Key_BindingForKey(int keynum) = 0;
  virtual int GetVGUI2KeyCodeForBind(const char *bind) = 0;
  virtual void GetVideoModes(void **liststart, int *count) = 0;
  virtual void GetCurrentVideoMode(int *wide, int *tall, int *bpp) = 0;
  virtual void GetCurrentRenderer(char *name, int namelen, int *windowed,
                                  int *hdmodels, int *addons,
                                  int *vidLevel) = 0;
  virtual bool IsConnectedToVACSecureServer() = 0;
  virtual int Key_KeyStringToKeyNum(const char *string) = 0;
};

bool currentVideoMode(int &width, int &height, int &bpp, bool &windowed) {
  if (!GameUIFuncs)
    return false;
  int w = 0, h = 0, depth = 0;
  GameUIFuncs->GetCurrentVideoMode(&w, &h, &depth); // 3 outputs: header matches
  if (w <= 0 || h <= 0)
    return false;
  char renderer[64] = {};
  int windowedFlag = 0;
  // The three extra outputs (hdmodels/addons/vid_level) are out of the T12
  // surface but MUST be passed: they are real argument slots of the engine
  // function, not optional. Scratch locals, values unused here.
  int hdmodels = 0, addons = 0, vidLevel = 0;
  auto *engine = reinterpret_cast<EngineGameUIFuncs *>(GameUIFuncs);
  engine->GetCurrentRenderer(renderer, static_cast<int>(sizeof(renderer)),
                             &windowedFlag, &hdmodels, &addons, &vidLevel);
  width = w;
  height = h;
  bpp = depth;
  windowed = windowedFlag != 0;
  return true;
}

int videoModeList(video::Mode *out, int max) {
  if (!GameUIFuncs || !out || max <= 0)
    return 0;
  EngineVideoMode *list = nullptr;
  int count = 0;
  GameUIFuncs->GetVideoModes(reinterpret_cast<struct vmode_s **>(&list),
                             &count);
  if (!list || count <= 0)
    return 0;
  if (count > max)
    count = max;
  for (int i = 0; i < count; ++i) {
    out[i].Width = list[i].Width;
    out[i].Height = list[i].Height;
    out[i].Bpp = list[i].Bpp;
  }
  return count;
}

bool showCursor() {
  if (!Surface2)
    return false;
  // Lock the selected native shape, not the pointer position: suppressed
  // GameUI hit testing must not replace it with dc_none on the next frame.
  Surface2->UnlockCursor();
  Surface2->SetCursor(vgui::dc_arrow);
  Surface2->LockCursor();
  return true;
}

void discardRelativeMouseMotion() {
  // Match client.dll's SDL2 import, NOT SDL3's float-output ABI. Resolve
  // each time: no cached function address may survive a module unload.
  using GetRelativeMouseState = unsigned int(__cdecl *)(int *, int *);
  if (const HMODULE sdl = GetModuleHandleA("SDL2.dll")) {
    const auto getRelative = reinterpret_cast<GetRelativeMouseState>(
        GetProcAddress(sdl, "SDL_GetRelativeMouseState"));
    if (getRelative) {
      int x = 0, y = 0;
      getRelative(&x, &y);
    }
  }
}

bool centerAndUnlockCursor() {
  if (!showCursor())
    return false;
  Surface2->SurfaceSetCursorPos(gEngfuncs.GetWindowCenterX(),
                                gEngfuncs.GetWindowCenterY());
  return true;
}

void lockCursor() {
  if (!Surface2)
    return;
  // Return control to VGUI, including an already-open GameUI/console.
  Surface2->UnlockCursor();
  Surface2->SetCursor(vgui::dc_none);
  Surface2->CalculateMouseVisible();
}

bool hideGameUi() {
  // The engine IBaseUI singleton, resolved from the same factory the
  // extension's BaseUI_Install used - so the vtable this pointer carries is
  // the hooked (proxied) one, and the call keeps the outer CBaseUI engine
  // root side effects while our inner takeover answers the queries.
  IBaseUI *BaseUi = baseUi();
  if (!BaseUi) {
    abc::ConsoleWarn("[AbcUi] IBaseUI unavailable - resume cannot run the "
                     "engine-side hide\n");
    return false;
  }
  BaseUi->HideGameUI();
  return true;
}

// Issue #5: the console counterpart. The outer IBaseUI::HideConsole body is
// where both the `hideconsole` command and the suppressed inner CGameUI::
// HideGameUI ended up: it flips the engine's own console-toggle flag and
// forwards to the proxied IGameConsole::Hide. The replacement console's
// close paths must run it too, or the stale flag re-enters the console on
// the next ActivateGameUI. The forwarding callback re-enters the bridge, so
// every caller lowers the replacement's presentation truth first.
bool hideConsole() {
  IBaseUI *BaseUi = baseUi();
  if (!BaseUi) {
    abc::ConsoleWarn("[AbcUi] IBaseUI unavailable - the engine-side console "
                     "close cannot run\n");
    return false;
  }
  BaseUi->HideConsole();
  return true;
}

} // namespace vgui2ext
