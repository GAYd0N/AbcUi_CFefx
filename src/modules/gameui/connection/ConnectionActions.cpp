// Typed connection intents; no command parsing or UI visibility policy.
#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/GameUiPages.h"
#include <abc/Console.h>
#include <string>
namespace gameui {
using namespace navigation;

// The retry body shared by the overlay's 重试 intent and the auto-retry
// beat: a NEW attempt generation for the same kept target, never a reuse of
// the dead one. T19: a failed LOCAL attempt replays its kept request through
// the same typed path. False = nothing to retry (the overlay stays honest).
bool GameUiModule::retryServerAttempt() {
  if (LastAttemptWasLocal && Create->replayPending()) {
    finishDispatch();
    return true;
  }
  const std::string address = LoadingModelState.address();
  if (address.empty())
    return false;
  Controller.dispatch(gameui::menu::makeConnectRequest(address.c_str()));
  finishDispatch();
  return true;
}

// The per-frame auto-retry beat (pumpBackgroundWork). The browser page owns
// the toggle's truth; AutoRetryGate owns the bounded, paced policy and fires
// only while the connection-error overlay is actually up.
void GameUiModule::tickAutoRetry() {
  const bool enabled = Browser && Browser->autoRetryJoin();
  if (!enabled) {
    AutoRetry.reset(); // the player switched it off: the chain ends
    return;
  }
  const bool failed =
      Controller.isMenuActive() &&
      Controller.state().overlays.contains(MenuOverlay::kConnectionError);
  if (!failed)
    return; // nothing to retry; the budget survives the next attempt
  if (!AutoRetry.tick(LastFrameTime, true, true))
    return;
  abc::ConsolePrint("[AbcUi] auto retry %d/%d\n", AutoRetry.attempts(),
                    services::loading::AutoRetryGate::kMaxAttempts);
  if (!retryServerAttempt())
    abc::ConsoleWarn("[AbcUi] auto retry had nothing to replay\n");
}

void GameUiModule::handleConnectionAction(ActionKind Kind) {
  if (Kind == ActionKind::kDirectConnect)
    Controller.dispatch(MenuEvent{EventKind::kDirectConnectToggle});
  else if (Kind == ActionKind::kLoadingCancel) {
    // The proven termination action (vanilla Cancel). The model latch
    // is the UI-edge guard (exactly one request per attempt); the
    // controller phase gate and the backend's generation latch are
    // the second and third guards behind it.
    const uint32_t live = LoadingModelState.generation();
    if (live != 0 && !LoadingModelState.requestCancel(live))
      return;          // already latched
    AutoRetry.reset(); // the player ended the chain
    Controller.dispatch(MenuEvent{EventKind::kConnectCancel});
  } else if (Kind == ActionKind::kErrorRetry) {
    // A manual retry starts a fresh auto-retry chain.
    AutoRetry.reset();
    if (!retryServerAttempt())
      return; // nothing to retry: the overlay stays honest
    return;
  } else if (Kind == ActionKind::kErrorReturn) {
    AutoRetry.reset();
    Controller.dispatch(MenuEvent{EventKind::kBack});
  } else if (Kind == ActionKind::kDirectConnectConnect) {
    if (!DirectConnect)
      return;
    const std::string address = DirectConnect->value();
    std::string host;
    int port = 0;
    if (!services::loading::LoadingModel::parseAddress(address.c_str(), host,
                                                       port)) {
      DirectConnect->showHint("#Loading_AddressInvalid");
      return; // stay put: nothing reaches the engine unvalidated
    }
    // A user-initiated attempt starts a fresh auto-retry chain.
    AutoRetry.reset();
    Controller.dispatch(gameui::menu::makeConnectRequest(address.c_str()));
  } else if (Kind == ActionKind::kDirectConnectCancel)
    Controller.dispatch(MenuEvent{EventKind::kBack});
  else if (Kind == ActionKind::kPasswordConnect) {
    if (!Password)
      return;
    const std::string password = Password->value();
    if (password.empty())
      return; // empty submissions are not typed intents
    Controller.dispatch(gameui::menu::makePasswordSubmitted(password.c_str()));
  } else if (Kind == ActionKind::kPasswordCancel)
    // Vanilla parity: closing the password dialog does not kill the
    // attempt (its backend Close only destroys the dialog). The
    // loading page stays; its Cancel is the termination path.
    Controller.dispatch(MenuEvent{EventKind::kBack});
  finishDispatch();
}
} // namespace gameui
