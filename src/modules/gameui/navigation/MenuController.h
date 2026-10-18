#pragma once

// gameui::menu::MenuController - the transition engine over MenuState (T04).
// Pure: it reads no VGUI pointers, issues no engine commands and never
// touches the Rml DOM. Events go in as value objects, backend work goes out
// through the injected services::IGameUiBackend, and presentation only reads
// state()/the derived queries. The exact production class is what
// tests/MenuStateMachine.cpp links - the test holds no model copy.
//
// FROZEN TRANSITION TABLE (T04 steps 3-6; every row is pinned by a test
// case in tests/MenuStateMachine.cpp):
//
// Global guard: once kShutdown latched (phase kShuttingDown) every further
// event is dropped and no action is ever emitted again (03 doc: no DOM, no
// backend work after Shutdown).
//
// Intents (one per physical interaction):
//   kOpenPage(sub)        menu up, phase InGame/Disconnected, not already
//                         on top              -> push page onto return stack
//   kBack                 top overlay is Emergency -> nothing (the emergency
//                         page is not dismissable; its quit path stays)
//   kBack                 overlays non-empty (top < Emergency) -> pop ONE
//   kBack                 top not Main/Pause/Loading -> pop (return to source)
//   kBack                 top Pause           -> close menu + resumeGame()
//   kBack                 top Main or Loading -> nothing (home/loading stay)
//   kMenuToggle           top overlay is Emergency -> nothing
//   kMenuToggle           overlays non-empty (top < Emergency) -> pop ONE
//   kMenuToggle           InGame, menu closed -> pages = [Pause]
//   kMenuToggle           InGame, top Pause   -> close menu + resumeGame()
//   kMenuToggle           InGame, top other   -> pop one page
//   kMenuToggle           no-map, top Main    -> nothing (home cannot vanish)
//   kMenuToggle           no-map, top other   -> pop one page
//   kMenuToggle           Connecting/Loading/Fatal -> nothing (cancel is the
//                         typed intent; loading UX is T14)
//   kResume               InGame, menu up     -> close menu + resumeGame()
//   kConsoleToggle        console absent      -> push Console overlay
//   kConsoleToggle        console present     -> remove it, order kept
//   kDirectConnectToggle  no-map phase        -> push/pop the DirectConnect
//                         overlay (T14 address entry; refused while an
//                         attempt/emergency holds the screen)
//   kConnectRequested     Booting/Disconnected, address non-empty
//                         -> new generation; Connecting; pages=[Loading];
//                         overlays+focus cleared; connect(gen, address copy)
//   kCreateGameRequested  Booting/Disconnected, map non-empty (T19)
//                         -> new generation; Connecting; pages=[Loading];
//                         overlays+focus cleared;
//                         startLocalGame(gen, map copy) - the SAME attempt
//                         machinery as a connect (one generation, one cancel
//                         path, one loading page); the payload is the validated
//                         map token and the backend owns the engine sequence
//   kConnectCancel        Connecting/Loading  -> retire generation;
//                         Disconnected; pages=[Main]; overlays cleared;
//                         cancelConnect(gen)
//   kDisconnectRequested  InGame              -> optimistic Disconnected;
//                         pages=[Main]; overlays+focus cleared;
//                         disconnectFromGame() (the backend phase event
//                         re-affirms idempotently)
//   kQuitRequested        menu up, no ConfirmQuit anywhere -> push it
//   kQuitResolved(yes)    top is ConfirmQuit  -> pop + quitGame()
//   kQuitResolved(no)     top is ConfirmQuit  -> pop
//   kPasswordSubmitted    top is Password, generation live, non-empty
//                         -> submitPassword(gen, copy); overlay stays until
//                         the backend answers
//
// Native events (deduped by (origin, seq); each origin layer tags all of
// its native callbacks from one monotonic counter, so seq never goes
// backwards within an origin; untagged/stale dropped silently):
//   kNativeActivate       InGame, menu closed -> pages=[Pause];
//                         no-map, menu closed -> pages=[Main];
//                         otherwise nothing - activation is idempotent, so
//                         dual-fired callers never toggle anything
//   kNativeHide           InGame, menu up     -> close menu, NO resumeGame()
//                         (engine-initiated; its pause/hideconsole
//                         side-effects are the hook layer's, T08)
//                         everything else -> REFUSED (menu cannot vanish
//                         no-map, loading holds input, emergency stays)
//   kNativeConsoleOpen    console absent      -> push Console overlay
//   kNativeConsoleClose   console present     -> remove it
//
// Bridge-sync events (T09 wiring; the suppression bridge coalesces the
// dual-fired engine callbacks before its sink fires, so these carry no
// sequence tag - one event per menu transition):
//   kMenuShown            idempotent open, same rows as kNativeActivate
//   kMenuHidden           pages and overlays force-cleared, phase kept, NO
//                         backend action (idempotent after a decided close)
//
// Backend events (stale = dropped silently; races resolve by generation):
//   kPhaseChanged(s,p)    s >= session: accept and bump; kDisconnected ->
//                         full cleanup pages=[Main] overlays+focus cleared
//                         generation retired; kInGame -> close menu;
//                         kBooting -> phase only; kFatal/kShuttingDown ->
//                         behave like their dedicated events;
//                         kConnecting/kLoading are generation-tagged only
//   kLoadingStarted(g)    g == live generation -> phase Loading
//   kLoadingFinished(g)   g == live generation:
//                         Success    -> InGame, menu closed, generation retired
//                         Failed     -> Disconnected, pages=[Main],
//                                       overlays=[ConnectionError], reason copy
//                         Password   -> push Password overlay, stay Connecting
//   kFatal(reason)        Fatal; pages cleared; overlays=[Emergency];
//                         reason copied; ESC/back refused there, quit path
//                         stays available
//   kShutdown             ShuttingDown; pages/overlays/generations/strings
//                         cleared; terminal
//
// T14 note: the loading stream keeps the T04 rows unchanged - the new
// kDirectConnectToggle row is the only added intent; the engine stream is
// translated by the module into the same generation-tagged kLoadingStarted/
// kLoadingFinished kinds (cancel/success/failure race-safe by construction).
//
// Ownership: the backend pointer is borrowed and may be null (actions are
// then dropped - presentation-only wiring); the controller never deletes it.

#include "MenuState.h"

#include "services/GameUiBridge.h"

namespace gameui {
namespace menu {

class MenuController {
public:
  explicit MenuController(services::IGameUiBackend *backend = nullptr);

  // Single entry point; `event` is taken by const& and copied, nothing
  // borrowed from it outlives the call.
  void dispatch(const MenuEvent &event);

  const MenuState &state() const {
    return State;
  }

  // Derived semantics (T04 step 5); invariants live on MenuState.
  bool isMenuActive() const {
    return State.isMenuActive();
  }
  bool hasExclusiveInput() const {
    return State.hasExclusiveInput();
  }
  int activationFlag() const {
    return State.activationFlag();
  }
  PointerTarget pointerTarget() const {
    return State.pointerTarget();
  }
  TextInputTarget textInputTarget() const {
    return State.textInputTarget();
  }
  bool canCloseMenu() const {
    return State.canCloseMenu();
  }

private:
  // Sequence filter for native events: true = first sight of
  // (origin, seq), false = untagged or re-delivered (drop).
  bool acceptNative(EventOrigin origin, uint32_t nativeSeq);

  // Transition helpers; each is idempotent under repeated delivery.
  void openPage(MenuPage page);
  void goBack();
  void menuToggle();
  void closeMenuAndResume();
  void toggleConsoleOverlay();
  void toggleDirectConnectOverlay();
  void openConsoleOverlay();
  void closeConsoleOverlay();
  void connectRequest(const std::string &address);
  void createGameRequest(const std::string &map);
  void cancelConnect();
  void disconnectRequest();
  void quitRequest();
  void quitResolve(bool confirmed);
  void passwordSubmit(const std::string &password);
  void nativeActivate();
  void nativeHide();
  void menuShown();
  void menuHidden();
  void applyPhaseChange(uint32_t session, BackendPhase phase);
  void loadingStarted(uint32_t generation);
  void loadingFinished(uint32_t generation, LoadingOutcome outcome,
                       const std::string &reason);
  void applyFatal(const std::string &reason);
  void applyShutdown();

  // Back to the no-map world: Main up, overlays and focus gone, attempt
  // retired, stale reason dropped. Idempotent.
  void enterDisconnected();
  void retireAttempt();

  MenuState State;
  services::IGameUiBackend *Backend;
  uint32_t NextGeneration = 1;    // monotonic, never reused, never reset
  uint32_t LastNativeSeq[3] = {}; // indexed by EventOrigin (kNone unused)
};

} // namespace menu
} // namespace gameui
