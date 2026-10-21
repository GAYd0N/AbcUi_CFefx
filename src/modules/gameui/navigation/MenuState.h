#pragma once
// Navigation value types, colocated with the controller and typed action
// adapter.

// gameui::menu value types (T04) - the state the future menu renderer reads
// and the value-type events MenuController consumes. Deliberately free of
// engine, VGUI and Rml includes so the exact production state machine links
// into offline tests (05 doc evidence tier 1).
//
// State axes are kept separate instead of boolean combinations (T04 step 1):
// the backend phase, the page navigation stack, the overlay stack and the
// connect generation each carry one bit of truth; everything else is derived
// (isMenuActive / hasExclusiveInput / pointer & text targets at the bottom).
// The phase is the ONLY source for "has a map": a live connection object is
// never consulted, Connecting/Loading are phases of their own (T04 step 5).

#include <cstddef>
#include <cstdint>
#include <string>

namespace gameui {
namespace menu {

// Backend lifecycle phase (03 doc). kBooting -> menu not presented yet;
// kDisconnected is the no-map world where the Main page cannot be dismissed;
// kConnecting/kLoading are owned by one generation-tagged connect attempt;
// kInGame is the only phase where closing the menu is legal; kShuttingDown
// is the terminal latch (every later event is dropped); kFatal hands the
// screen to the Emergency overlay.
enum class BackendPhase {
  kBooting,
  kDisconnected,
  kConnecting,
  kLoading,
  kInGame,
  kShuttingDown,
  kFatal,
};

// Pages (03 doc). kMain exists only in no-map phases; kPause is the in-game
// home; kLoading is what a connect attempt shows while it owns the menu;
// the rest push onto the return stack, so Settings opened from Main returns
// to Main and Settings opened from Pause returns to Pause (T04 step 3).
enum class MenuPage {
  kMain,
  kPause,
  kSettings,
  kServers,
  kCreateGame,
  kPlayerList,
  kVoteMenu,
  kCommandMenu,
  kInventory,
  kLoading,
};

// Overlays (03 doc). The top-most overlay holds exclusive menu input and one
// ESC pops exactly one layer - a confirm/password under the console is still
// there when the console closes (T04 step 3). T14 adds kDirectConnect: the
// basic "host[:port]" entry reachable from the no-map home before the server
// browser exists (T18 replaces the entry with the full flow).
enum class MenuOverlay {
  kConsole,
  kConfirmQuit,
  kPassword,
  kConnectionError,
  kDirectConnect,
  kEmergency,
};

// Which native wrapper layer delivered an event. BaseUI-outer and
// GameUI-inner callbacks both observe the same logical events
// (hook-contract.md §3/T6); T08 tags the source, the controller dedupes on
// (origin, sequence) instead of letting repeated toggles cancel out.
enum class EventOrigin {
  kNone = 0,
  kBaseUi,
  kGameUi,
};

enum class EventKind {
  // UI intents. One event per physical interaction; the future input
  // router (T06) guarantees single delivery, so these carry no sequence
  // tag and repeated identical intents stay individually idempotent.
  kOpenPage,
  kBack,
  kMenuToggle, // the ESC semantics: open Pause / pop top layer / resume
  kResume,
  kConsoleToggle,
  kDirectConnectToggle, // T14: push/pop the address-entry overlay
  kConnectRequested,
  kCreateGameRequested, // T19: start a local game (payload = map name)
  kConnectCancel,
  kDisconnectRequested,
  kQuitRequested,
  kQuitResolved,
  kPasswordSubmitted,

  // Native hook events, tagged with origin + per-origin monotonic
  // sequence (>= 1). Untagged or re-delivered ones are dropped.
  kNativeActivate,     // ActivateGameUI observed: idempotent open
  kNativeHide,         // HideGameUI observed: idempotent close, refused no-map
  kNativeConsoleOpen,  // GameConsole activate observed
  kNativeConsoleClose, // GameConsole hide observed

  // Bridge-sync events (T09 wiring). The suppression bridge coalesces the
  // dual-fired engine callbacks before its sink fires, so the module sees
  // exactly one event per menu transition - no sequence tag needed.
  kMenuShown,  // the bridge raised the menu: idempotent open (Main no-map,
               // Pause in-game), nothing while an attempt/emergency holds
  kMenuHidden, // the bridge lowered the menu from outside the state
               // machine's decision (focus loss, teardown): force-clear
               // pages and overlays, phase kept, never a backend action

  // Backend state events.
  kPhaseChanged,    // session-tagged backend phase; stale sessions dropped
  kLoadingStarted,  // generation-tagged
  kLoadingFinished, // generation-tagged: cancel/success/failure race-safe
  kMapLoadStarted,  // the engine began a level load that no connect attempt
                    // owns (a server changelevel): the Loading page takes the
                    // screen instead of the no-map Main page
  kFatal,           // engine/renderer fatal; Emergency overlay takes over
  kShutdown,        // terminal latch; afterwards no action is ever emitted
};

enum class LoadingOutcome {
  kSuccess,
  kFailed,
  kPasswordRequired,
};

// T19: which kind of attempt owns the live generation. The phase/loading
// machinery is shared (every attempt owns the screen, streams the engine
// loading events and cancels through the proven disconnect); only the retry
// path and the loading title presentation differ.
enum class AttemptKind {
  kNone = 0,
  kConnect,   // remote connect (T14/T18): retry replays the address
  kLocalGame, // local server start (T19): retry replays the kept request
  kMapLoad,   // engine-initiated changelevel (no backend action, no retry)
};

enum class PointerTarget {
  kGame,
  kMenu,
};

enum class TextInputTarget {
  kNone,
  kConsole,
  kPasswordField,
};

// One value-type event; strings are owned copies, never borrowed pointers.
// Which fields carry meaning per kind:
//   page             - kOpenPage
//   origin/nativeSeq - the four kNative* kinds
//   text             - kConnectRequested (address), kCreateGameRequested
//                      (map name), kPasswordSubmitted (password),
//                      kLoadingFinished (failure reason), kFatal (reason).
//                      Data payloads only; user strings never become command
//                      names (T04 step 2).
//   phase/session    - kPhaseChanged (session is the backend's monotonic
//                      cycle id; anything below the accepted session is stale)
//   generation       - kLoadingStarted, kLoadingFinished
//   outcome          - kLoadingFinished
//   confirmed        - kQuitResolved
struct MenuEvent {
  EventKind kind;
  MenuPage page = MenuPage::kMain;
  BackendPhase phase = BackendPhase::kBooting;
  EventOrigin origin = EventOrigin::kNone;
  uint32_t nativeSeq = 0;
  uint32_t session = 0;
  uint32_t generation = 0;
  LoadingOutcome outcome = LoadingOutcome::kFailed;
  bool confirmed = false;
  std::string text;
};

// Event factories for the payload-carrying kinds; the bare kinds are
// aggregate-initialized directly (MenuEvent{EventKind::kBack}).
inline MenuEvent makeOpenPage(MenuPage page) {
  MenuEvent event{EventKind::kOpenPage};
  event.page = page;
  return event;
}
inline MenuEvent makeConnectRequest(const char *address) {
  MenuEvent event{EventKind::kConnectRequested};
  event.text = address;
  return event;
}
inline MenuEvent makeCreateGameRequest(const char *map) {
  MenuEvent event{EventKind::kCreateGameRequested};
  event.text = map ? map : "";
  return event;
}
inline MenuEvent makeQuitResolved(bool confirmed) {
  MenuEvent event{EventKind::kQuitResolved};
  event.confirmed = confirmed;
  return event;
}
inline MenuEvent makePasswordSubmitted(const char *password) {
  MenuEvent event{EventKind::kPasswordSubmitted};
  event.text = password;
  return event;
}
inline MenuEvent makeNative(EventKind kind, EventOrigin origin,
                            uint32_t nativeSeq) {
  MenuEvent event{kind};
  event.origin = origin;
  event.nativeSeq = nativeSeq;
  return event;
}
inline MenuEvent makePhaseChanged(uint32_t session, BackendPhase phase) {
  MenuEvent event{EventKind::kPhaseChanged};
  event.session = session;
  event.phase = phase;
  return event;
}
inline MenuEvent makeLoadingStarted(uint32_t generation) {
  MenuEvent event{EventKind::kLoadingStarted};
  event.generation = generation;
  return event;
}
inline MenuEvent makeLoadingFinished(uint32_t generation,
                                     LoadingOutcome outcome,
                                     const char *reason) {
  MenuEvent event{EventKind::kLoadingFinished};
  event.generation = generation;
  event.outcome = outcome;
  event.text = reason ? reason : "";
  return event;
}
// Engine-initiated map load (changelevel). Carries no target/generation: the
// controller allocates the generation and the module starts the loading model
// from `state().connectGeneration` after the dispatch.
inline MenuEvent makeMapLoadStarted() {
  return MenuEvent{EventKind::kMapLoadStarted};
}
inline MenuEvent makeFatal(const char *reason) {
  MenuEvent event{EventKind::kFatal};
  event.text = reason ? reason : "";
  return event;
}

// Fixed-capacity stack: no allocation, deterministic caps; push on a full
// stack fails and the caller treats it as "unrepresentable depth dropped"
// (caps are far above any reachable sequence).
template <typename T, size_t kCapacity> class FixedStack {
public:
  bool push(T value) {
    if (Count == kCapacity)
      return false;
    Items[Count++] = value;
    return true;
  }
  // Only legal when !empty(); every caller guards.
  void pop() {
    --Count;
  }
  bool empty() const {
    return Count == 0;
  }
  size_t size() const {
    return Count;
  }
  const T &top() const {
    return Items[Count - 1];
  }
  bool topIs(T value) const {
    return Count != 0 && Items[Count - 1] == value;
  }
  bool contains(T value) const {
    for (size_t i = 0; i < Count; i++) {
      if (Items[i] == value)
        return true;
    }
    return false;
  }
  // Removes the (single) occurrence, keeping the order of the rest.
  bool remove(T value) {
    for (size_t i = 0; i < Count; i++) {
      if (Items[i] == value) {
        for (size_t j = i + 1; j < Count; j++)
          Items[j - 1] = Items[j];
        --Count;
        return true;
      }
    }
    return false;
  }
  void clear() {
    Count = 0;
  }
  const T *begin() const {
    return Items;
  }
  const T *end() const {
    return Items + Count;
  }

private:
  T Items[kCapacity] = {};
  size_t Count = 0;
};

constexpr size_t kMaxPages = 8;
constexpr size_t kMaxOverlays = 8;

struct MenuState {
  BackendPhase phase = BackendPhase::kBooting;
  FixedStack<MenuPage, kMaxPages> pages;          // bottom = home page
  FixedStack<MenuOverlay, kMaxOverlays> overlays; // top = exclusive input
  // Highest accepted backend session; phase events below it are stale.
  uint32_t session = 0;
  // Live connect/create attempt; 0 = none. Generations are handed out by the
  // controller, monotonic, never reused - a cancelled/superseded attempt
  // retires its generation and any late result for it is dropped.
  uint32_t connectGeneration = 0;
  std::string connectAddress; // owned copy of the live attempt target
  // T19: what the live (or just-finished, for the retry decision) attempt
  // was. Set when an attempt starts; a successful/cancelled attempt clears
  // it, a failed one KEEPS it so the error overlay's retry can replay the
  // right kind.
  AttemptKind attemptKind = AttemptKind::kNone;
  std::string errorReason; // owned copy feeding ConnectionError/Emergency

  // Derived outputs (T04 step 5). The shipped GameUI implements
  // HasExclusiveInput as a forward to IsGameUIActive (hook-contract.md §4),
  // so the replacement keeps both the same BY CONSTRUCTION; activationFlag
  // feeds the int-typed vtable slots (ActivateGameUI returns int, the two
  // queries return bool - hook-contract.md T2).
  bool isMenuActive() const {
    return !pages.empty() || !overlays.empty();
  }
  bool hasExclusiveInput() const {
    return isMenuActive();
  }
  int activationFlag() const {
    return isMenuActive() ? 1 : 0;
  }
  PointerTarget pointerTarget() const {
    return isMenuActive() ? PointerTarget::kMenu : PointerTarget::kGame;
  }
  // Closing the menu is a game-only privilege: no-map Main must stay,
  // Connecting/Loading hold the input, Fatal shows the emergency page.
  bool canCloseMenu() const {
    return phase == BackendPhase::kInGame;
  }
  // The console hotkey's in-game close intent: the plain pause page with
  // the console as its only overlay returns all the way to gameplay in one
  // press. The no-map home stays (it cannot vanish), and a deeper page or
  // another modal keeps its context - only the console layer closes there.
  bool consoleHotkeyClosesMenu() const {
    return phase == BackendPhase::kInGame && pages.size() == 1 &&
           pages.topIs(MenuPage::kPause) && overlays.size() == 1 &&
           overlays.topIs(MenuOverlay::kConsole);
  }
  // Text input goes to the top text-capable overlay; page editors
  // (connect address field etc.) arrive with T06/T07.
  TextInputTarget textInputTarget() const {
    if (overlays.empty())
      return TextInputTarget::kNone;
    switch (overlays.top()) {
    case MenuOverlay::kConsole:
      return TextInputTarget::kConsole;
    case MenuOverlay::kPassword:
      return TextInputTarget::kPasswordField;
    default:
      return TextInputTarget::kNone;
    }
  }
};

} // namespace menu
} // namespace gameui
