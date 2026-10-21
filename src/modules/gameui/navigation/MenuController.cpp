// gameui::menu::MenuController implementation - see MenuController.h for the
// frozen transition table this file realizes. No engine/VGUI/Rml dependency:
// the whole translation unit links into offline tests unchanged.
#include "MenuController.h" // navigation domain; no platform/UI dependency

namespace gameui {
namespace menu {

namespace {

bool isSubPage(MenuPage page) {
  switch (page) {
  case MenuPage::kSettings:
  case MenuPage::kServers:
  case MenuPage::kCreateGame:
  case MenuPage::kPlayerList:
  case MenuPage::kVoteMenu:
  case MenuPage::kCommandMenu:
  case MenuPage::kInventory:
    return true;
  default:
    return false; // kMain/kPause/kLoading are transition-managed only
  }
}

bool isNoMapPhase(BackendPhase phase) {
  return phase == BackendPhase::kBooting ||
         phase == BackendPhase::kDisconnected;
}

} // namespace

MenuController::MenuController(services::IGameUiBackend *backend)
    : Backend(backend) {}

bool MenuController::acceptNative(EventOrigin origin, uint32_t nativeSeq) {
  if ((origin != EventOrigin::kBaseUi && origin != EventOrigin::kGameUi) ||
      nativeSeq == 0)
    return false; // untagged: a wiring bug, not a toggle
  const size_t index = static_cast<size_t>(origin);
  if (nativeSeq <= LastNativeSeq[index])
    return false; // re-delivery of an already applied event
  LastNativeSeq[index] = nativeSeq;
  return true;
}

void MenuController::dispatch(const MenuEvent &event) {
  if (State.phase == BackendPhase::kShuttingDown)
    return; // terminal: events dropped, actions never again

  switch (event.kind) {
  case EventKind::kOpenPage:
    openPage(event.page);
    break;
  case EventKind::kBack:
    goBack();
    break;
  case EventKind::kMenuToggle:
    menuToggle();
    break;
  case EventKind::kResume:
    closeMenuAndResume();
    break;
  case EventKind::kConsoleToggle:
    toggleConsoleOverlay();
    break;
  case EventKind::kDirectConnectToggle:
    toggleDirectConnectOverlay();
    break;
  case EventKind::kConnectRequested:
    connectRequest(event.text);
    break;
  case EventKind::kCreateGameRequested:
    createGameRequest(event.text);
    break;
  case EventKind::kConnectCancel:
    cancelConnect();
    break;
  case EventKind::kDisconnectRequested:
    disconnectRequest();
    break;
  case EventKind::kQuitRequested:
    quitRequest();
    break;
  case EventKind::kQuitResolved:
    quitResolve(event.confirmed);
    break;
  case EventKind::kPasswordSubmitted:
    passwordSubmit(event.text);
    break;
  case EventKind::kNativeActivate:
    if (acceptNative(event.origin, event.nativeSeq))
      nativeActivate();
    break;
  case EventKind::kNativeHide:
    if (acceptNative(event.origin, event.nativeSeq))
      nativeHide();
    break;
  case EventKind::kNativeConsoleOpen:
    if (acceptNative(event.origin, event.nativeSeq))
      openConsoleOverlay();
    break;
  case EventKind::kNativeConsoleClose:
    if (acceptNative(event.origin, event.nativeSeq))
      closeConsoleOverlay();
    break;
  case EventKind::kMenuShown:
    menuShown();
    break;
  case EventKind::kMenuHidden:
    menuHidden();
    break;
  case EventKind::kPhaseChanged:
    applyPhaseChange(event.session, event.phase);
    break;
  case EventKind::kLoadingStarted:
    loadingStarted(event.generation);
    break;
  case EventKind::kLoadingFinished:
    loadingFinished(event.generation, event.outcome, event.text);
    break;
  case EventKind::kMapLoadStarted:
    mapLoadStarted();
    break;
  case EventKind::kFatal:
    applyFatal(event.text);
    break;
  case EventKind::kShutdown:
    applyShutdown();
    break;
  }
}

void MenuController::openPage(MenuPage page) {
  if (!isSubPage(page))
    return;
  if (!isNoMapPhase(State.phase) && State.phase != BackendPhase::kInGame)
    return; // Connecting/Loading hold input, Fatal is emergency-only
  if (State.pages.empty())
    return; // the menu must already be up (in-game entries come with T06/T09)
  if (State.pages.top() == page)
    return; // repeated intent: idempotent
  State.pages.push(page);
}

void MenuController::goBack() {
  if (!State.overlays.empty()) {
    if (State.overlays.top() == MenuOverlay::kEmergency)
      return; // the emergency page is not dismissable (quit path only)
    State.overlays.pop(); // exactly one layer per press
    return;
  }
  if (State.pages.empty())
    return;
  switch (State.pages.top()) {
  case MenuPage::kMain:
  case MenuPage::kLoading:
    break; // no-map home cannot vanish; loading is owned by its attempt
  case MenuPage::kPause:
    closeMenuAndResume();
    break;
  default:
    State.pages.pop(); // sub-pages return to their source
    break;
  }
}

void MenuController::menuToggle() {
  if (!State.overlays.empty()) {
    if (State.overlays.top() == MenuOverlay::kEmergency)
      return; // the emergency page is not dismissable (quit path only)
    State.overlays.pop();
    return;
  }
  switch (State.phase) {
  case BackendPhase::kInGame:
    if (State.pages.empty()) {
      State.pages.push(MenuPage::kPause);
      return;
    }
    if (State.pages.top() == MenuPage::kPause) {
      closeMenuAndResume();
      return;
    }
    State.pages.pop();
    return;
  case BackendPhase::kBooting:
  case BackendPhase::kDisconnected:
    // No-map: the home page cannot be dismissed, deeper pages return.
    if (!State.pages.empty() && State.pages.top() != MenuPage::kMain)
      State.pages.pop();
    return;
  case BackendPhase::kConnecting:
  case BackendPhase::kLoading:
  case BackendPhase::kFatal:
    return; // held by the attempt / emergency screen
  case BackendPhase::kShuttingDown:
    return; // unreachable (dispatch guard)
  }
}

void MenuController::closeMenuAndResume() {
  if (!State.canCloseMenu() || !State.isMenuActive())
    return; // no-map/loading/fatal or already closed: no backend action
  State.pages.clear();
  State.overlays.clear();
  if (Backend)
    Backend->resumeGame();
}

void MenuController::toggleConsoleOverlay() {
  if (State.overlays.contains(MenuOverlay::kConsole))
    State.overlays.remove(MenuOverlay::kConsole);
  else
    State.overlays.push(MenuOverlay::kConsole);
}

void MenuController::toggleDirectConnectOverlay() {
  if (State.overlays.contains(MenuOverlay::kDirectConnect)) {
    State.overlays.remove(MenuOverlay::kDirectConnect);
    return;
  }
  // The address entry belongs to the no-map world (the in-game connect
  // sequence is T17/T19's); an attempt or the emergency screen keeps the
  // screen (same refusal set as the other open intents).
  if (!isNoMapPhase(State.phase))
    return;
  if (State.overlays.contains(MenuOverlay::kEmergency))
    return;
  State.overlays.push(MenuOverlay::kDirectConnect);
}

void MenuController::openConsoleOverlay() {
  if (!State.overlays.contains(MenuOverlay::kConsole))
    State.overlays.push(MenuOverlay::kConsole);
}

void MenuController::closeConsoleOverlay() {
  State.overlays.remove(MenuOverlay::kConsole);
}

void MenuController::connectRequest(const std::string &address) {
  if (address.empty())
    return;
  // In-game connects go through disconnect first; the browser/create-game
  // wiring owns that sequence (T17/T19).
  if (!isNoMapPhase(State.phase))
    return;
  const uint32_t generation = NextGeneration++;
  State.connectGeneration = generation;
  State.connectAddress = address; // owned copy
  State.attemptKind = AttemptKind::kConnect;
  State.phase = BackendPhase::kConnecting;
  State.pages.clear();
  State.pages.push(MenuPage::kLoading);
  State.overlays
      .clear(); // stale error/password overlays die with the new attempt
  State.errorReason.clear();
  if (Backend)
    Backend->connect(generation, State.connectAddress.c_str());
}

// T19: a local-game start is the SAME attempt machinery as a remote connect
// (one generation, one Loading page, one cancel path) - the payload is the
// validated map token, and the backend decides the engine sequence. The
// address slot carries the map name so the retry path and the loading title
// work without a second value store.
void MenuController::createGameRequest(const std::string &map) {
  if (map.empty())
    return;
  if (!isNoMapPhase(State.phase))
    return;
  const uint32_t generation = NextGeneration++;
  State.connectGeneration = generation;
  State.connectAddress = map; // owned copy (the attempt target)
  State.attemptKind = AttemptKind::kLocalGame;
  State.phase = BackendPhase::kConnecting;
  State.pages.clear();
  State.pages.push(MenuPage::kLoading);
  State.overlays.clear();
  State.errorReason.clear();
  if (Backend)
    Backend->startLocalGame(generation, State.connectAddress.c_str());
}

void MenuController::cancelConnect() {
  if (State.phase != BackendPhase::kConnecting &&
      State.phase != BackendPhase::kLoading)
    return;
  const uint32_t generation = State.connectGeneration;
  retireAttempt();
  State.phase = BackendPhase::kDisconnected;
  State.pages.clear();
  State.pages.push(MenuPage::kMain);
  State.overlays.clear();
  if (Backend && generation != 0)
    Backend->cancelConnect(generation);
}

void MenuController::disconnectRequest() {
  if (State.phase != BackendPhase::kInGame)
    return;
  // Optimistic: the menu shows the disconnected world immediately (original
  // UX parity); the backend's phaseChanged(kDisconnected) re-affirms.
  State.phase = BackendPhase::kDisconnected;
  State.pages.clear();
  State.pages.push(MenuPage::kMain);
  State.overlays.clear(); // pages and focus reset (T04 step 3)
  if (Backend)
    Backend->disconnectFromGame();
}

void MenuController::quitRequest() {
  if (!State.isMenuActive())
    return;
  if (State.overlays.contains(MenuOverlay::kConfirmQuit))
    return; // repeated intent: one confirm, not a stack
  State.overlays.push(MenuOverlay::kConfirmQuit);
}

void MenuController::quitResolve(bool confirmed) {
  // Only the top-most overlay is interactive, so resolve requires it on top;
  // a confirm buried under the console cannot be confirmed through them.
  if (!State.overlays.topIs(MenuOverlay::kConfirmQuit))
    return;
  State.overlays.pop();
  if (confirmed && Backend)
    Backend->quitGame();
}

void MenuController::passwordSubmit(const std::string &password) {
  if (!State.overlays.topIs(MenuOverlay::kPassword))
    return;
  if (State.connectGeneration == 0 || password.empty())
    return;
  if (Backend)
    Backend->submitPassword(State.connectGeneration, password.c_str());
  // The overlay stays until the backend answers for this generation.
}

void MenuController::nativeActivate() {
  switch (State.phase) {
  case BackendPhase::kInGame:
    if (State.pages.empty())
      State.pages.push(MenuPage::kPause);
    return;
  case BackendPhase::kBooting:
  case BackendPhase::kDisconnected:
    if (State.pages.empty())
      State.pages.push(MenuPage::kMain);
    return; // already up: dual-fired callers change nothing
  case BackendPhase::kConnecting:
  case BackendPhase::kLoading:
  case BackendPhase::kFatal:
  case BackendPhase::kShuttingDown:
    return; // the attempt / emergency owns the screen
  }
}

void MenuController::nativeHide() {
  if (!State.canCloseMenu())
    return; // no-map refusal, loading hold, emergency stay
  State.pages.clear();
  State.overlays.clear();
  // No resumeGame(): the engine initiated this hide; the original
  // unpause/hideconsole side-effects belong to the hook layer (T08).
}

void MenuController::menuShown() {
  // Same open rows as nativeActivate without the (origin, seq) gate: the
  // suppression bridge coalesces the dual-fired engine callbacks before its
  // sink fires, so one event per transition arrives here (T09 wiring).
  switch (State.phase) {
  case BackendPhase::kInGame:
    if (State.pages.empty())
      State.pages.push(MenuPage::kPause);
    return;
  case BackendPhase::kBooting:
  case BackendPhase::kDisconnected:
    if (State.pages.empty())
      State.pages.push(MenuPage::kMain);
    return; // already up: the sink fired once per transition anyway
  case BackendPhase::kConnecting:
  case BackendPhase::kLoading:
  case BackendPhase::kFatal:
  case BackendPhase::kShuttingDown:
    return; // the attempt / emergency owns the screen
  }
}

void MenuController::menuHidden() {
  // The bridge lowered the menu from outside the state machine's decision
  // (focus loss, teardown): force-clear, phase kept, never a backend action.
  // After a decided close this is the idempotent second half of the sync.
  State.pages.clear();
  State.overlays.clear();
}

void MenuController::applyPhaseChange(uint32_t session, BackendPhase phase) {
  if (session < State.session)
    return; // stale session: dropped (T04 step 3)
  switch (phase) {
  case BackendPhase::kDisconnected:
    State.session = session;
    enterDisconnected();
    break;
  case BackendPhase::kInGame:
    State.session = session;
    retireAttempt();
    State.phase = BackendPhase::kInGame;
    State.pages.clear();
    State.overlays.clear();
    break;
  case BackendPhase::kBooting:
    State.session = session;
    State.phase = BackendPhase::kBooting;
    break;
  case BackendPhase::kFatal:
    State.session = session;
    applyFatal(std::string());
    break;
  case BackendPhase::kShuttingDown:
    applyShutdown();
    break;
  case BackendPhase::kConnecting:
  case BackendPhase::kLoading:
    break; // generation-tagged events only; an untagged claim is ignored
  }
}

void MenuController::loadingStarted(uint32_t generation) {
  if (generation == 0 || generation != State.connectGeneration)
    return; // cancelled/superseded: race-safe drop
  if (State.phase != BackendPhase::kConnecting)
    return;
  State.phase = BackendPhase::kLoading;
}

void MenuController::mapLoadStarted() {
  switch (State.phase) {
  case BackendPhase::kConnecting:
  case BackendPhase::kLoading:
    return; // a live connect/local attempt owns this loading stream
  case BackendPhase::kFatal:
  case BackendPhase::kShuttingDown:
    return; // emergency/terminal screens stay
  case BackendPhase::kBooting:
  case BackendPhase::kDisconnected:
  case BackendPhase::kInGame:
    break;
  }
  // The engine's changelevel: it is already loading, so this transition
  // emits no backend action. Allocating an ordinary generation lets the
  // existing model/stream/retire machinery own the progress values and the
  // ordinary kLoadingFinished rows end it (Success -> InGame, Failed ->
  // Disconnected + error). A stale attempt generation is retired first.
  retireAttempt();
  const uint32_t generation = NextGeneration++;
  State.connectGeneration = generation;
  State.connectAddress.clear(); // no user-supplied target for a map load
  State.attemptKind = AttemptKind::kMapLoad;
  State.phase = BackendPhase::kLoading;
  State.pages.clear();
  State.pages.push(MenuPage::kLoading);
  State.overlays.clear();
  State.errorReason.clear();
}

void MenuController::loadingFinished(uint32_t generation,
                                     LoadingOutcome outcome,
                                     const std::string &reason) {
  if (generation == 0 || generation != State.connectGeneration)
    return; // a retired generation can never finish (cancel race)
  switch (outcome) {
  case LoadingOutcome::kSuccess:
    retireAttempt();
    State.phase = BackendPhase::kInGame;
    State.pages.clear();
    State.overlays.clear();
    break;
  case LoadingOutcome::kPasswordRequired:
    if (State.phase != BackendPhase::kConnecting &&
        State.phase != BackendPhase::kLoading)
      return;
    State.phase = BackendPhase::kConnecting;
    if (!State.overlays.contains(MenuOverlay::kPassword))
      State.overlays.push(MenuOverlay::kPassword);
    break;
  case LoadingOutcome::kFailed:
    retireAttempt();
    enterDisconnected();
    State.errorReason = reason; // owned copy, read by the error overlay
    State.overlays.push(MenuOverlay::kConnectionError);
    break;
  }
}

void MenuController::applyFatal(const std::string &reason) {
  retireAttempt();
  State.phase = BackendPhase::kFatal;
  State.pages.clear();
  State.overlays.clear();
  State.overlays.push(MenuOverlay::kEmergency);
  State.errorReason = reason;
}

void MenuController::applyShutdown() {
  retireAttempt();
  State.phase = BackendPhase::kShuttingDown;
  State.pages.clear();
  State.overlays.clear();
  State.connectAddress.clear();
  State.errorReason.clear();
  // Terminal: the dispatch guard drops everything after this point, so no
  // DOM can be created and no backend action can ever be emitted again.
}

void MenuController::enterDisconnected() {
  retireAttempt();
  State.phase = BackendPhase::kDisconnected;
  State.pages.clear();
  State.pages.push(MenuPage::kMain);
  State.overlays.clear();
  State.errorReason.clear();
}

void MenuController::retireAttempt() {
  State.connectGeneration = 0;
  State.connectAddress.clear();
  // The kind describes the LIVE attempt only; once it ends the module's
  // pending value objects own the retry decision (T19: the failed attempt's
  // request is kept module-side, exactly like PendingHistory).
  State.attemptKind = AttemptKind::kNone;
}

} // namespace menu
} // namespace gameui
