#pragma once

// services::GameUiBridge - the typed backend seam between the pure menu state
// machine (gameui::menu, T04) and the engine/GameUI integration layer that a
// later task (T09+) completes. T04 froze the IGameUiBackend type contract;
// T08 adds the native-UI suppression bridge (NativeUiBridge) on the inbound
// side of the same seam.
//
// Direction of data:
//   OUT  menu decisions      - IGameUiBackend below, injected into
//                              gameui::menu::MenuController; the state machine
//                              never reads VGUI pointers and never issues an
//                              engine command itself (T04 step 4).
//   IN   backend/native state- gameui::menu::MenuEvent value objects fed to
//                              MenuController::dispatch. Loading events are
//                              generation-tagged, phase events session-tagged,
//                              native hook events carry origin + sequence so
//                              dual-fired callbacks dedupe (hook-contract.md
//                              T5/T6). All strings arrive copied.
//
// Hard rules the implementations must keep once this seam is wired:
//   - User-provided strings (server address, password, error text) travel as
//     data payloads only; they must never become console command names
//     (T04 step 2). The original command chains (connect, password %s +
//     retry, disconnect, quit) are rebuilt INSIDE a backend implementation,
//     one method per intent.
//   - Single-player pause is NOT part of this contract: whether the original
//     setpause/unpause behaviour is needed awaits backend proof (T04 step 5,
//     hook-contract.md §4); resumeGame() covers the menu-side close only.
//   - Hook installation and chain ownership stay with the spine/integration
//     layer (coding rule 5); this header never names a hook.

#include <cstddef>
#include <cstdint>
#include <string>

namespace services {

// Executor of the typed menu actions (T04 step 2). Implementations copy any
// string they keep past the call; calls happen synchronously inside
// MenuController::dispatch on the main thread.
class IGameUiBackend {
public:
  virtual ~IGameUiBackend() = default;

  // Start connecting to `address`. `generation` is the state machine's
  // attempt id; later cancelConnect/submitPassword and the backend's own
  // loading events must carry the same generation so stale results of a
  // superseded attempt are dropped.
  virtual void connect(uint32_t generation, const char *address) = 0;
  // T19: start a LOCAL server on `map` (the validated enumerated token).
  // The backend owns the shipped launch sequence; the attempt machinery is
  // the SAME as connect (one generation, one Loading page, one cancel), and
  // success is still owned by the engine's loading stream - this call never
  // declares a join.
  virtual void startLocalGame(uint32_t generation, const char *map) = 0;
  // Give up on the live attempt (loading cancel path).
  virtual void cancelConnect(uint32_t generation) = 0;
  // Password retry path of the live attempt (original password %s + retry
  // chain; the real chain is T14/T17 wiring).
  virtual void submitPassword(uint32_t generation, const char *password) = 0;
  // Leave the current game (original Disconnect command backend).
  virtual void disconnectFromGame() = 0;
  // Confirmed quit (the confirm dialog is menu-side state; only the
  // confirmed decision reaches here, original QuitNoConfirm branch).
  virtual void quitGame() = 0;
  // Close the menu and hand input back to the game (original ResumeGame /
  // HideGameUI decision made menu-side). Issued only when the state machine
  // can actually close (in-game); never unconditionally.
  virtual void resumeGame() = 0;
};

// T08: the native-UI suppression bridge. Pure state + rules consumed by the
// engine-coupled adapters (rmlui/VGUI2ExtensionImport.cpp entry layer and
// rmlui/PaintGate.cpp paint/input gate); kept in its own translation unit so
// the exact production decision logic links into offline tests (05 doc tier
// 1) while hook/extension headers stay out of the test binary.
//
// What lives where (T08 hard split):
//   - This class: the rules. Which modules are native UI; when the gate is
//     armed; whether the menu is up; the engine-facing answers; the counters
//     the acceptance reads.
//   - The adapters: the seams. VFTHook install/uninstall with validation,
//     callback registration, turning rules into Result/return values.
//
// Engine evidence for the entry-layer shapes (Ghidra, install hw.dll
// e3c7f374 / GameUI.dll 99382b87; RVAs, image base 0x01d00000):
//   - The window-level key handler (0x10460) consumes ESC itself: on ESC-down
//     it reads IGameUI +0x2c (IsGameUIActive) and calls IBaseUI +0x1c
//     (HideGameUI) when the menu is active and a map is loaded, otherwise
//     IBaseUI +0x20 (ActivateGameUI); it then returns "handled", which is why
//     physical ESC never reaches the client export chain (T06/T07 evidence).
//   - A second toggle helper (0x16b80) does the same checks through
//     IGameUI +0x18 (HasExclusiveInput) - the two queries must answer alike
//     because both helpers exist (hook-contract §4: the shipped GameUI
//     forwards one to the other; we keep them identical by construction).
//   - IBaseUI (CBaseUI) ActivateGameUI/HideGameUI call their IGameUI
//     counterparts and then flip the ENGINE VGUI roots themselves (0x10680
//     activate / 0x105f0 hide). The extension proxies both layers, so
//     intercepting the INNER (IGameUI) layer suppresses the vanilla
//     GameUI.dll internals (panel flips, setpause/unpause/hideconsole) while
//     the outer layer still performs the engine-root side effects - the
//     "necessary engine side effects" the takeover must preserve.

enum class NativeUiOrigin : int {
  kBaseUi = 0,  // BaseUI-outer wrapper (engine CBaseUI Activate/Hide/Console)
  kGameUi,      // GameUI-inner wrapper (IGameUI Activate/Hide, direct callers)
  kGameConsole, // legacy GameConsole wrapper (console open intent)
  kOwnUi,       // our own page show/hide action (router owner / debug seam)
  kCount,
};

// PaintTraverse/hit-test attribution. Attempts and blocked counts are
// tracked per bucket; the acceptance requires the two native buckets'
// "allowed" counts (attempts - blocked) to be 0 while armed (05 doc V01/V08).
enum class PaintBucket : int {
  kGameUi = 0,    // module string "GameUI"
  kServerBrowser, // module string "ServerBrowser" (Sven main-menu dialog +
                  // popups)
  kOther,         // BaseUI roots, Client, foreign plugins - always allowed
  kCount,
};

// One engine show/hide request decision, returned to the entry-layer adapter.
enum class ToggleAction : int {
  kNone,       // nothing beyond consuming (disarmed, idempotent, duplicate
               // dispatch of the same engine call, or a hide the vanilla
               // wrapper would also have skipped)
  kOpened,     // the menu went up (page sink already ran)
  kRouteClose, // menu up with a page: the adapter routes one synthetic ESC
               // press through rmlui::routeKey so the page owner keeps its
               // once-per-press semantics (T06/T07: composition cancel
               // first, then close); the owner lowers via requestMenuDown
  kStateClose, // menu up without a page (Rml not ready): the adapter
               // lowers directly - no owner exists to route to
};

// The page side of the seam (gameui module): the bridge decides WHEN the
// menu is up; the sink implementation owns HOW the minimal page is presented
// and taken down. Injected once at module construction; never owned.
class INativeMenuSink {
public:
  virtual ~INativeMenuSink() = default;
  // Menu raised (cold-start Activate included): present the minimal page.
  // Called from inside the engine callback - the implementation must not
  // re-enter the bridge.
  virtual void onNativeMenuShow() = 0;
  // Menu lowered through any path (ESC route, engine hide, focus loss):
  // take the page down.
  virtual void onNativeMenuHide() = 0;
  // T16 replacement-console entries: an engine source asked for the console
  // (GameConsole Activate from -console/-toconsole or direct callers, BaseUI
  // ShowConsole from the toggle console binding) or for its close
  // (GameConsole Hide, BaseUI HideConsole, the hideconsole command).
  // requestConsoleOpen has already raised the menu when needed; both
  // intents are idempotent here. Default no-ops keep pure sinks compiling.
  virtual void onNativeConsoleOpen() {}
  virtual void onNativeConsoleClose() {}
};

struct NativeUiCounters {
  // Paint/input gate.
  uint32_t PaintAttempts[(int)PaintBucket::kCount] = {};
  uint32_t PaintBlocked[(int)PaintBucket::kCount] = {};
  uint32_t HitTestAttempts[(int)PaintBucket::kCount] = {};
  uint32_t HitTestBlocked[(int)PaintBucket::kCount] = {};
  // Native panels whose keyboard/mouse input flags this plugin cleared
  // (the input gate; steady state is a fixed number, growth means new
  // native panels appeared).
  uint32_t InputFlagsCleared = 0;
  // Entry layer (IGameUI funnel).
  uint32_t ActivateRequests[(int)NativeUiOrigin::kCount] = {};
  uint32_t HideRequests[(int)NativeUiOrigin::kCount] = {};
  uint32_t MenuUpTransitions = 0;
  uint32_t MenuDownTransitions = 0;
  uint32_t PageShows = 0;
  uint32_t PageHides = 0;
  // Engine-facing observability.
  uint32_t ExclusiveQueries = 0;
  uint32_t ActiveQueries = 0;
  uint32_t ConsoleActivates = 0;
  uint32_t ConsoleHides = 0;
  uint32_t ConsoleVisibleQueries = 0;
  uint32_t RunFrames = 0;
  uint32_t LoadingStarts = 0;
  uint32_t LoadingFinishes = 0;
  uint32_t Disconnects = 0;
  // Entry-layer actions.
  uint32_t BaseUiToggles = 0;   // BaseUI-layer Activate/Hide observations
  uint32_t BaseUiKeyEvents = 0; // keys the BaseUI seam kept off the vanilla UI
  uint32_t EscRouted = 0;       // close presses routed through the router
  uint32_t EscRepeatsSuppressed = 0; // held-ESC repeats refused a second route
  uint32_t PhysicalEscUps = 0;       // physical ESC releases observed
};

class NativeUiBridge {
public:
  // The native-UI module rule: exact, case-insensitive string identity with
  // the two module names this install reports (T02 §6: "GameUI" for
  // GameUI.dll panels - BasePanel/TaskBar/dialogs/console popup - and
  // "ServerBrowser" for the Sven main-menu dialog and every popup it
  // detaches; read through IPanel::GetModuleName so panel creation/
  // destruction/recreation needs no cache and no VPANEL identity).
  // Everything else - engine roots, client module, foreign plugins - passes.
  static bool nativeUiModule(const char *module);

  NativeUiBridge() = default;

  // Gate lifecycle: armed by the install seam inside the BaseUI::Initialize
  // callback (T02 §5: before GameUI::Start and the first native paint) and
  // only ever disarmed during controlled teardown after the native UI can
  // no longer paint (strict mode never falls back to the original UI -
  // T08 step 7 / 03 doc fault section).
  // T16: a console entry that arrives BEFORE the arm (the -console /
  // -toconsole startup path runs in the native BaseUI::Initialize body,
  // which precedes the proxy's Initialize dispatch where the gate arms)
  // is latched as a pending intent and delivered exactly once at arm()
  // time - the startup console presents after takeover, never the legacy
  // one.
  void arm();
  void disarm();
  bool armed() const {
    return Armed;
  }

  // PaintTraverse gate: true = swallow the traversal (never call the
  // original). Disarmed attempts pass untouched; attempts/blocked are
  // counted either way so the acceptance can show both numbers.
  bool gatePaintAttempt(const char *module);
  // Mouse hit-test gate (IPanel::IsWithinTraverse): same rule, same counter
  // style. A blocked hit test reports "no panel here", so the old UI can
  // never be the mouse-over target.
  bool gateHitTestAttempt(const char *module);
  void noteInputFlagsCleared(uint32_t count) {
    Counters.InputFlagsCleared += count;
  }

  // Menu visibility state. Both directions are idempotent and report
  // whether a transition happened, so the sink presents/hides the page
  // exactly once regardless of the pre+post double dispatch. MenuUp is the
  // single truth the engine answers derive from.
  bool requestMenuUp(NativeUiOrigin origin);
  bool requestMenuDown(NativeUiOrigin origin);
  bool menuUp() const {
    return MenuUp;
  }

  // T16 console presentation (the single truth behind IGameConsole::
  // IsConsoleVisible and the sink's console overlay). Engine console
  // entries converge here: the GameConsole Activate/Hide callbacks
  // (-console, -toconsole, direct callers) and the BaseUI ShowConsole/
  // HideConsole callbacks (the `toggleconsole`/`hideconsole` command
  // path). requestConsoleOpen raises the menu first when it is down
  // (vanilla: opening the console activates GameUI), then notifies the
  // sink; requestConsoleClose lowers only the console - the menu beneath
  // stays (ESC parity: one layer per event). Both are idempotent and
  // report a state transition. The UI-side dismissal (routed ESC, the
  // close button) goes through noteConsoleDismissed - the module IS the
  // sink, no callback echo.
  bool requestConsoleOpen(NativeUiOrigin origin);
  bool requestConsoleClose(NativeUiOrigin origin);
  void noteConsoleDismissed();
  bool consoleRequested() const {
    return ConsoleRequested;
  }
  // T16 no-map close route: with no level loaded the engine's window
  // handler turns ESC into ActivateGameUI (a no-op once the menu is up),
  // so the close never arrives through engineHide. The surface ESC-down
  // observation consumes this gate instead - one route per physical press,
  // shared with engineHide's latch (the mark clears on the physical key-up
  // and at every menu raise). Returns false when this press was already
  // routed (hold repeats).
  bool consumeEscRoute();

  // The inner (IGameUI) decision points, driven from the entry-layer
  // adapter. engineHide coalesces the wrapper's duplicate dispatch
  // (IsPost lies there, hook-contract T5) AND refuses a second close route
  // for the same physical ESC press: the route is marked once per press and
  // the mark clears on the physical key-up (noteEscPhysicalUp, observed at
  // the raw surface app-event seam) or at the next menu raise. A hold
  // therefore runs exactly one transition ("一物理按键仅一次转场", V03),
  // while the composition case still works: press 1 cancels the
  // composition, release, press 2 routes the close.
  ToggleAction engineActivate();
  ToggleAction engineHide();
  // Physical ESC release observed (SDL key-up for the ESC keycode at the
  // surface app-event callback; pure observation, never consumed). Also
  // honored idempotently - the callback dispatches twice.
  void noteEscPhysicalUp() {
    EscPressRouted = false;
    ++Counters.PhysicalEscUps;
  }
  void noteBaseUiToggle() {
    ++Counters.BaseUiToggles;
  }
  void noteBaseUiKeyEvent() {
    ++Counters.BaseUiKeyEvents;
  }

  // Page sink injection (gameui module); the bridge calls
  // onNativeMenuShow/Hide on the transitions above. Null = no page wiring.
  void setMenuSink(INativeMenuSink *sink) {
    MenuSink = sink;
  }

  // Level/session facts observed from the GameUI lifecycle callbacks; the
  // hide gate mirrors the shipped CGameUI::HideGameUI (connected-only:
  // a no-map ESC leaves the home page up - T04).
  bool hasLevel() const {
    return HasLevel;
  }
  void noteLevelLoaded() {
    HasLevel = true;
  }
  void noteDisconnected() {
    HasLevel = false;
    ++Counters.Disconnects;
  }

  // Derived engine-facing answers (IGameUI types: HasExclusiveInput int,
  // IsGameUIActive bool - hook-contract T2).
  bool isGameUIActive() const {
    return MenuUp;
  }
  bool hasExclusiveInput() const {
    return MenuUp;
  }
  // T16: the engine-facing IGameConsole answer now mirrors the replacement
  // console's presentation (false when it is not up). One truth: the flag
  // the entry callbacks drive; the native console can never be visible
  // (its Activate is consumed and its paint is gated), so this can only
  // report OUR console.
  bool consoleVisible() const {
    return ConsoleRequested;
  }

  // Page mirror, driven by the sink implementation (the bridge cannot know
  // Rml readiness). The acceptance compares it with the menu state so a
  // page out of step is visible in the counters.
  void notePageShown() {
    PageShown = true;
    ++Counters.PageShows;
  }
  void notePageHidden() {
    PageShown = false;
    ++Counters.PageHides;
  }
  bool pageShown() const {
    return PageShown;
  }

  void noteEscRouted() {
    ++Counters.EscRouted;
  }
  void noteExclusiveQuery() {
    ++Counters.ExclusiveQueries;
  }
  void noteActiveQuery() {
    ++Counters.ActiveQueries;
  }
  void noteConsoleActivate() {
    ++Counters.ConsoleActivates;
  }
  void noteConsoleHide() {
    ++Counters.ConsoleHides;
  }
  void noteConsoleVisibleQuery() {
    ++Counters.ConsoleVisibleQueries;
  }
  void noteRunFrame() {
    ++Counters.RunFrames;
  }
  void noteLoadingStart() {
    ++Counters.LoadingStarts;
  }
  void noteLoadingFinish() {
    ++Counters.LoadingFinishes;
  }

  // Snapshot; counters keep accumulating.
  NativeUiCounters counters() const {
    return Counters;
  }

private:
  INativeMenuSink *MenuSink = nullptr;
  NativeUiCounters Counters;
  bool Armed = false;
  bool MenuUp = false;
  bool PageShown = false;
  bool HasLevel = false;
  // T16 console presentation truth (see requestConsoleOpen). The pending
  // latches hold pre-arm entries (see the arm() note).
  bool ConsoleRequested = false;
  bool PendingConsoleOpen = false;
  bool PendingConsoleClose = false;
  // One close route per physical ESC press (see engineHide).
  bool EscPressRouted = false;
};

// The production instance. The adapters (VGUI2ExtensionImport entry layer,
// PaintGate) and the gameui module all read the same truth.
NativeUiBridge &nativeUiBridge();

// --- T14: the loading feed -------------------------------------------------
// The IGameUI loading callbacks (slots 12..19) deliver borrowed pointers that
// are only valid inside the synchronous callback (hook-contract T7). The
// entry layer copies every value into this queue; the gameui module drains it
// on its frame and feeds LoadingModel + the menu controller. The queue is
// main-thread-only by construction (both the engine UI callbacks and the
// module frame run on it): its job is copying and deferring, never locking.
enum class LoadingFeedKind : int {
  kLoadingStarted = 0,
  kLoadingFinished,
  kStartProgress,
  kContinueProgress,
  kStopProgress,
  kStatusText,
  kSecondaryProgress,
  kSecondaryText,
  kPasswordPrompt, // T14: engine asks for the server password (+0x58)
  kCount,
};

struct LoadingFeedEvent {
  LoadingFeedKind kind = LoadingFeedKind::kLoadingStarted;
  std::string text;  // resourceType / statusText / failureReason
  std::string text2; // resourceName / extendedReason
  int point = 0;
  int steps = 0;
  float fraction = 0.0f;
  bool error = false;
};

class LoadingFeed {
public:
  // Bounded: the oldest event is dropped when a burst overflows (progress is
  // a "latest value wins" stream; the newest data must survive). Dropped
  // counts are observable for the acceptance.
  static constexpr size_t kCapacity = 128;
  void push(const LoadingFeedEvent &event);
  bool pop(LoadingFeedEvent &out);
  uint32_t dropped() const {
    return Dropped;
  }

private:
  LoadingFeedEvent Events[kCapacity];
  size_t Head = 0;
  size_t Count = 0;
  uint32_t Dropped = 0;
};

LoadingFeed &loadingFeed();

// T14: the semantic step one copied engine event carries for a live attempt.
// The module's frame drain dispatches the controller's generation-tagged
// kinds from this classification; the offline test pins the table without
// linking the engine-coupled module.
enum class LoadingStep : int {
  kNone = 0,         // value-only update (progress/status), no transition
  kStarted,          // a LoadingStarted on the live attempt
  kJoined,           // LoadingFinished("level") - the engine-confirmed join
  kFailed,           // StopProgressBar(error) - failure with a reason
  kPasswordRequired, // the engine asked for the server password
};

LoadingStep classifyLoadingEvent(const LoadingFeedEvent &event);

} // namespace services
