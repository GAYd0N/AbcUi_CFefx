// vgui2ext::NativeEntryCallbacks implementation - see NativeEntryCallbacks.h.
// The three callback objects moved here verbatim from VGUI2ExtensionImport.cpp
// (T08); T21a changed only: post-phase Result is OVERRIDE for the return-value
// callbacks (value re-assert without a second skip), Disconnect/RunFrame
// observation is pre-only (one logical action per engine call), and the
// engine-coupled calls go through NativeEntrySeams.
#include "NativeEntryCallbacks.h"

#include "../services/GameUiBridge.h"
#include "../services/console/ConsoleBuffer.h"

#include <cstring>

namespace vgui2ext {

namespace {

NativeEntrySeams Seams;

void routeEscPress() {
  if (Seams.routeEscPress)
    Seams.routeEscPress();
}

bool menuOwned() {
  return Seams.menuOwned && Seams.menuOwned();
}

// The hook-contract T2 rule: write return values through the proxy-owned
// storage with the REAL type of the wrapped method (int slots via int*,
// bool slots via bool*). A wrong cast silently corrupts the return.
void writePluginInt(VGUI2Extension_CallbackContext *ctx, int value) {
  if (ctx && ctx->pPluginReturnValue)
    *static_cast<int *>(ctx->pPluginReturnValue) = value;
}

void writePluginBool(VGUI2Extension_CallbackContext *ctx, bool value) {
  if (ctx && ctx->pPluginReturnValue)
    *static_cast<bool *>(ctx->pPluginReturnValue) = value;
}

// T08 entry layer: the inner IGameUI funnel. Everything that opens or closes
// the native menu converges here (the engine's ESC/key handlers and native
// UI route through ActivateGameUI/HideGameUI, directly or
// via the IBaseUI methods the extension proxies). While the bridge is armed
// these handlers answer the two queries from the replacement state and
// consume the two actions, so the vanilla CGameUI internals (panel flips,
// setpause/unpause/hideconsole) never run. The outer CBaseUI layer keeps the
// engine-root side effects of the toggle.
class NativeGameUICallbacks final : public IVGUI2Extension_GameUICallbacks {
public:
  int GetAltitude() const override {
    return 0;
  }

  void Initialize([[maybe_unused]] CreateInterfaceFn *factories,
                  [[maybe_unused]] int count) override {}
  void PreStart([[maybe_unused]] struct cl_enginefuncs_s *engineFuncs,
                [[maybe_unused]] int interfaceVersion,
                [[maybe_unused]] void *system) override {}
  void Start([[maybe_unused]] struct cl_enginefuncs_s *engineFuncs,
             [[maybe_unused]] int interfaceVersion,
             [[maybe_unused]] void *system) override {
    // T14: with GameUI.dll mapped and started, the Sven password-prompt
    // slot gets its observation hook.
    if (Seams.installPasswordPromptHook)
      Seams.installPasswordPromptHook();
  }
  void Shutdown() override {
    // Fallback only: on this extension pin the notification does NOT
    // fire at engine exit (the proxy unhooks its IGameUI slots before
    // the original Shutdown would dispatch it). Harmless when it does
    // fire: the gate uninstall is idempotent.
    if (Seams.detachPaintGate)
      Seams.detachPaintGate();
  }
  void PostShutdown() override {}

  void
  ActivateGameUI(VGUI2Extension_CallbackContext *CallbackContext) override {
    auto &bridge = services::nativeUiBridge();
    if (!bridge.armed())
      return; // before the gate exists the vanilla path stays
    if (!CallbackContext->IsPost) {
      bridge.engineActivate();            // the one business action, pre only
      writePluginInt(CallbackContext, 1); // vanilla ActivateGameUI returns 1
      CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
    } else {
      // Post Result was reset by the proxy: re-assert the value claim.
      writePluginInt(CallbackContext, 1);
      CallbackContext->Result = VGUI2Extension_Result::OVERRIDE;
    }
  }

  void ActivateDemoUI([[maybe_unused]] VGUI2Extension_CallbackContext
                          *CallbackContext) override {
    // Not in scope (no demo UI); vanilla behaviour. Note: this wrapper
    // dispatches post unconditionally (hook-contract T4) - never rely on
    // it for one-shot work.
  }

  void
  HasExclusiveInput(VGUI2Extension_CallbackContext *CallbackContext) override {
    auto &bridge = services::nativeUiBridge();
    if (!bridge.armed())
      return;
    if (!CallbackContext->IsPost) {
      bridge.noteExclusiveQuery();
      writePluginInt(CallbackContext, bridge.hasExclusiveInput() ? 1 : 0);
      CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
    } else {
      writePluginInt(CallbackContext, bridge.hasExclusiveInput() ? 1 : 0);
      CallbackContext->Result = VGUI2Extension_Result::OVERRIDE;
    }
  }

  void RunFrame(VGUI2Extension_CallbackContext *CallbackContext) override {
    // Never suppressed (T08 step 3: the original keeps its necessary
    // background frame work; the paint gate, not this callback, is what
    // keeps its output invisible). Pre-only observation (one count per
    // engine frame; the post dispatch is the proxy's echo).
    if (!CallbackContext->IsPost)
      services::nativeUiBridge().noteRunFrame();
  }

  void ConnectToServer([[maybe_unused]] const char *&game,
                       [[maybe_unused]] int &IP, [[maybe_unused]] int &port,
                       [[maybe_unused]] VGUI2Extension_CallbackContext
                           *CallbackContext) override {
    // Connect is T14/T17 territory; the vanilla window never opens
    // because the menu answers stay ours and the paint gate is armed,
    // but the engine-side connect chain keeps working.
  }
  void DisconnectFromServer(
      VGUI2Extension_CallbackContext *CallbackContext) override {
    // Pre-only: the level fact is a state mutation (and the counter a
    // logical-event count) - the proxy's post dispatch must not run it
    // twice.
    if (!CallbackContext->IsPost) {
      services::nativeUiBridge().noteDisconnected();
      if (Seams.onDisconnected)
        Seams.onDisconnected();
    }
  }
  void HideGameUI(VGUI2Extension_CallbackContext *CallbackContext) override {
    auto &bridge = services::nativeUiBridge();
    if (!bridge.armed())
      return;
    // NOTE: no IsPost branch - at this pin CGameUIProxy::HideGameUI
    // dispatches pre AND post with IsPost=false in both phases
    // (hook-contract T5). The bridge coalesces the duplicate dispatch AND
    // the held-ESC repeats into one route.
    const auto action = bridge.engineHide();
    if (action == services::ToggleAction::kRouteClose) {
      // One synthetic ESC press through the input router: the page
      // owner keeps its once-per-press semantics (T07: a running
      // composition is cancelled first, the next press closes the
      // page and releases ownership).
      bridge.noteEscRouted();
      routeEscPress();
      // The page was shown but no owner could act (torn-down router):
      // lower the state directly instead of leaving a phantom menu.
      if (bridge.menuUp() && !menuOwned())
        bridge.requestMenuDown(services::NativeUiOrigin::kBaseUi);
    } else if (action == services::ToggleAction::kStateClose) {
      // Menu up without a page (Rml not ready): state-only close.
      bridge.requestMenuDown(services::NativeUiOrigin::kBaseUi);
    }
    // The vanilla CGameUI::HideGameUI never runs while armed: its
    // unpause/hideconsole (T09 decides their replacement) and its panel
    // flips are original-UI behaviour. The outer CBaseUI::HideGameUI
    // still performs the engine-root side effects.
    CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
  }
  void
  IsGameUIActive(VGUI2Extension_CallbackContext *CallbackContext) override {
    auto &bridge = services::nativeUiBridge();
    if (!bridge.armed())
      return;
    if (!CallbackContext->IsPost) {
      bridge.noteActiveQuery();
      writePluginBool(CallbackContext, bridge.isGameUIActive());
      CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
    } else {
      writePluginBool(CallbackContext, bridge.isGameUIActive());
      CallbackContext->Result = VGUI2Extension_Result::OVERRIDE;
    }
  }

  void
  LoadingStarted(const char *&resourceType, const char *&resourceName,
                 VGUI2Extension_CallbackContext *CallbackContext) override {
    // T14: the engine stream enters the loading feed here - the borrowed
    // pointers are copied before the callback returns (hook-contract T7);
    // the vanilla handler still runs (observed, never superceded): its
    // non-visual bookkeeping stays intact and its dialog stays paint-gated.
    if (CallbackContext->IsPost)
      return;
    services::nativeUiBridge().noteLoadingStart();
    services::LoadingFeedEvent event;
    event.kind = services::LoadingFeedKind::kLoadingStarted;
    event.text = resourceType ? resourceType : "";
    event.text2 = resourceName ? resourceName : "";
    services::loadingFeed().push(event);
  }
  void
  LoadingFinished(const char *&resourceType, const char *&resourceName,
                  VGUI2Extension_CallbackContext *CallbackContext) override {
    if (CallbackContext->IsPost)
      return;
    auto &bridge = services::nativeUiBridge();
    bridge.noteLoadingFinish();
    // A finished level load is the engine-confirmed "in game" fact the
    // vanilla connected-only hide gate reads.
    if (resourceType && !_stricmp(resourceType, "level"))
      bridge.noteLevelLoaded();
    services::LoadingFeedEvent event;
    event.kind = services::LoadingFeedKind::kLoadingFinished;
    event.text = resourceType ? resourceType : "";
    event.text2 = resourceName ? resourceName : "";
    services::loadingFeed().push(event);
  }
  void
  StartProgressBar(const char *&progressType, int &progressSteps,
                   VGUI2Extension_CallbackContext *CallbackContext) override {
    if (CallbackContext->IsPost)
      return;
    services::LoadingFeedEvent event;
    event.kind = services::LoadingFeedKind::kStartProgress;
    event.text = progressType ? progressType : "";
    event.steps = progressSteps;
    services::loadingFeed().push(event);
  }
  void ContinueProgressBar(
      int &progressPoint, float &progressFraction,
      VGUI2Extension_CallbackContext *CallbackContext) override {
    if (CallbackContext->IsPost)
      return;
    services::LoadingFeedEvent event;
    event.kind = services::LoadingFeedKind::kContinueProgress;
    event.point = progressPoint;
    event.fraction = progressFraction;
    services::loadingFeed().push(event);
  }
  void
  StopProgressBar(bool &bError, const char *&failureReason,
                  const char *&extendedReason,
                  VGUI2Extension_CallbackContext *CallbackContext) override {
    if (CallbackContext->IsPost)
      return;
    services::LoadingFeedEvent event;
    event.kind = services::LoadingFeedKind::kStopProgress;
    event.error = bError;
    event.text = failureReason ? failureReason : "";
    event.text2 = extendedReason ? extendedReason : "";
    services::loadingFeed().push(event);
  }
  void SetProgressBarStatusText(
      const char *&statusText,
      VGUI2Extension_CallbackContext *CallbackContext) override {
    if (CallbackContext->IsPost)
      return;
    services::LoadingFeedEvent event;
    event.kind = services::LoadingFeedKind::kStatusText;
    event.text = statusText ? statusText : "";
    services::loadingFeed().push(event);
  }
  void SetSecondaryProgressBar(
      float &progress,
      VGUI2Extension_CallbackContext *CallbackContext) override {
    if (CallbackContext->IsPost)
      return;
    services::LoadingFeedEvent event;
    event.kind = services::LoadingFeedKind::kSecondaryProgress;
    event.fraction = progress;
    services::loadingFeed().push(event);
  }
  void SetSecondaryProgressBarText(
      const char *&statusText,
      VGUI2Extension_CallbackContext *CallbackContext) override {
    if (CallbackContext->IsPost)
      return;
    services::LoadingFeedEvent event;
    event.kind = services::LoadingFeedKind::kSecondaryText;
    event.text = statusText ? statusText : "";
    services::loadingFeed().push(event);
  }
  const char *GetControlModuleName() const override {
    return "AbcUi";
  }
};

NativeGameUICallbacks GameUICallbacksInstance;

// T08 step 5: the legacy GameConsole never presents. Activation is consumed
// (the replacement console page stands in; the startup -console path is T16),
// visibility answers from the bridge, and its paint is blocked by the
// traversal gate already.
class NativeGameConsoleCallbacks final
    : public IVGUI2Extension_GameConsoleCallbacks {
public:
  int GetAltitude() const override {
    return 0;
  }

  void Activate(VGUI2Extension_CallbackContext *CallbackContext) override {
    // T16: the entry funnel of the replacement console. The startup
    // -console/-toconsole path calls this directly (hw.dll native
    // BaseUI::Initialize -> GameConsole::Activate); before the arm the
    // bridge latches the intent and vanilla proceeds (pre-arm window),
    // after the arm the call is consumed. The ` command path (the engine
    // `toggleconsole`/`-condebug` chain -> IBaseUI::ShowConsole -> the
    // real CBaseUI -> this proxied IGameConsole instance) lands here as
    // well, so one funnel owns every engine-side open.
    auto &bridge = services::nativeUiBridge();
    if (!CallbackContext->IsPost)
      bridge.noteConsoleActivate();
    bridge.requestConsoleOpen(services::NativeUiOrigin::kGameConsole);
    if (bridge.armed()) {
      // The toggle key's own committed character ("`"/"~" from SDL
      // text input) belongs to the toggle, never to a focused editor
      // (vanilla parity: the key does not type). Arm the swallow here
      // so the engine-driven open cannot leak the character into the
      // freshly focused editor either.
      if (Seams.swallowToggleKeyText)
        Seams.swallowToggleKeyText();
      CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
    }
  }
  void Initialize([[maybe_unused]] VGUI2Extension_CallbackContext
                      *CallbackContext) override {}
  void Hide(VGUI2Extension_CallbackContext *CallbackContext) override {
    auto &bridge = services::nativeUiBridge();
    if (!CallbackContext->IsPost)
      bridge.noteConsoleHide();
    bridge.requestConsoleClose(services::NativeUiOrigin::kGameConsole);
    if (bridge.armed())
      CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
  }
  void Clear(VGUI2Extension_CallbackContext *CallbackContext) override {
    // T15: the engine's `clear` command wipes the notify context and then
    // calls this (hw.dll 0x01d40b70 -> Clear helper -> vtable slot 4). The
    // mirror must follow: clear() drops the cached lines AND the pending
    // fragment, bumps the generation. The pre+post double dispatch
    // collapses to one clear (ConsoleCapture).
    services::console::consoleCapture().onClear(CallbackContext->IsPost);
  }
  void
  IsConsoleVisible(VGUI2Extension_CallbackContext *CallbackContext) override {
    auto &bridge = services::nativeUiBridge();
    if (!bridge.armed())
      return;
    if (!CallbackContext->IsPost) {
      bridge.noteConsoleVisibleQuery();
      writePluginBool(CallbackContext, bridge.consoleVisible());
      CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
    } else {
      writePluginBool(CallbackContext, bridge.consoleVisible());
      CallbackContext->Result = VGUI2Extension_Result::OVERRIDE;
    }
  }
  void Printf(IVGUI2Extension_String *str,
              VGUI2Extension_CallbackContext *CallbackContext) override {
    // T15: consume the formatted text into the bounded cache - a byte
    // copy taken inside the synchronous callback (borrowed string), one
    // cached line per engine message (the capture dedups the proxy's
    // pre+post echo on the wrapper string identity). Result stays UNSET:
    // the real print path runs untouched (the engine console buffer, the
    // -condebug file log and the notify area are the engine's own).
    // Never print from inside this callback: it would re-enter here and
    // double-write the file log (hook-contract T8).
    services::console::consoleCapture().onPrintf(
        str, str ? str->data() : nullptr, str ? str->length() : 0,
        CallbackContext->IsPost);
  }
  void DPrintf(IVGUI2Extension_String *str,
               VGUI2Extension_CallbackContext *CallbackContext) override {
    // T15: same seam. DPrintf only reaches this callback when the engine's
    // developer gate above the vtable call already passed (hw.dll
    // FUN_01d03b90 tests the developer cvar before the slot+0x1c call),
    // so the cache contains exactly the console's developer-mode lines;
    // the per-line Developer flag keeps that distinction for the UI.
    services::console::consoleCapture().onDPrintf(
        str, str ? str->data() : nullptr, str ? str->length() : 0,
        CallbackContext->IsPost);
  }
  void SetParent([[maybe_unused]] vgui::VPANEL parent,
                 [[maybe_unused]] VGUI2Extension_CallbackContext
                     *CallbackContext) override {
    // Parent wiring is engine plumbing; the original keeps it.
  }
};

NativeGameConsoleCallbacks GameConsoleCallbacksInstance;

// T08 uninstall seam. The notification that fires while vgui2.dll is still
// fully alive is a ClientVGUI Shutdown - it is the proxy's very first step
// (upstream BaseUI.cpp:158-176), before the IGameUI proxies come down and
// before the original Shutdown unloads GameUI.dll/vgui2.dll. The IPanel
// hooks must be removed here: leaving them installed past the vgui2 unload
// turns the engine's own teardown into a call through an unmapped vtable
// (observed as an 0xC0000005 exit).
class NativeClientVGUICallbacks final
    : public IVGUI2Extension_ClientVGUICallbacks {
public:
  int GetAltitude() const override {
    return 0;
  }
  void Initialize([[maybe_unused]] CreateInterfaceFn *factories,
                  [[maybe_unused]] int count) override {}
  void Shutdown() override {
    // vgui2 alive; unhook the IPanel gate and disarm. The strict
    // takeover rule holds: this runs only on the engine teardown path
    // (ClientVGUI is created at BaseUI::Start and destroyed here), not
    // on map changes, so suppression never has a gap while the original
    // UI could still paint.
    if (Seams.detachPaintGate)
      Seams.detachPaintGate();
    // T14: the GameUI instance is still mapped here (GameUI.dll unloads
    // in the original CBaseUI::Shutdown, after this notification) - the
    // password-prompt slot hook comes down with the gate.
    if (Seams.detachPasswordPromptHook)
      Seams.detachPasswordPromptHook();
  }
  void Start() override {}
  void SetParent([[maybe_unused]] vgui::VPANEL parent) override {}
  void UseVGUI1([[maybe_unused]] VGUI2Extension_CallbackContext
                    *CallbackContext) override {}
  void HideScoreBoard([[maybe_unused]] VGUI2Extension_CallbackContext
                          *CallbackContext) override {}
  void HideAllVGUIMenu([[maybe_unused]] VGUI2Extension_CallbackContext
                           *CallbackContext) override {}
  void ActivateClientUI([[maybe_unused]] VGUI2Extension_CallbackContext
                            *CallbackContext) override {}
  void HideClientUI([[maybe_unused]] VGUI2Extension_CallbackContext
                        *CallbackContext) override {}
};

NativeClientVGUICallbacks ClientVGUICallbacksInstance;

} // namespace

void setNativeEntrySeams(const NativeEntrySeams &seams) {
  Seams = seams;
}

IVGUI2Extension_GameUICallbacks &gameUICallbacks() {
  return GameUICallbacksInstance;
}
IVGUI2Extension_GameConsoleCallbacks &gameConsoleCallbacks() {
  return GameConsoleCallbacksInstance;
}
IVGUI2Extension_ClientVGUICallbacks &clientVGUICallbacks() {
  return ClientVGUICallbacksInstance;
}

} // namespace vgui2ext
