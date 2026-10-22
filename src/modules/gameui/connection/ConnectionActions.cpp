// Typed connection intents; no command parsing or UI visibility policy.
#include "modules/gameui/GameUiModule.h"
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
  // The toggle's truth lives in the session (the page module publishes
  // it through the sdk serverbrowser section).
  const bool enabled = ServerBrowser.AutoRetryJoin;
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

// The sdk connection section's intent boundaries. The controller-layer
// decisions stay here; the page module reaches them through the section.

bool GameUiModule::connectionCancelLoading() {
  // The proven termination action (vanilla Cancel): the model latch is the
  // UI-edge guard; the controller phase gate and the backend's generation
  // latch sit behind it.
  const uint32_t live = LoadingModelState.generation();
  if (live == 0 || !LoadingModelState.requestCancel(live))
    return false;
  AutoRetry.reset(); // the player ended the chain
  Controller.dispatch(MenuEvent{EventKind::kConnectCancel});
  finishDispatch();
  return true;
}

void GameUiModule::connectionRetryFailed() {
  // A manual retry starts a fresh auto-retry chain.
  AutoRetry.reset();
  if (!retryServerAttempt())
    return; // nothing to retry: the overlay stays honest
  finishDispatch();
}

void GameUiModule::connectionReturnHome() {
  AutoRetry.reset();
  Controller.dispatch(MenuEvent{EventKind::kBack});
  finishDispatch();
}

bool GameUiModule::connectionSubmitPassword(const char *password) {
  if (!password || !*password)
    return false; // empty submissions are not typed intents
  Controller.dispatch(gameui::menu::makePasswordSubmitted(password));
  finishDispatch();
  return true;
}

void GameUiModule::connectionPasswordCancel() {
  // Vanilla parity: closing the password dialog does not kill the attempt
  // (its backend Close only destroys the dialog). The loading page stays;
  // its Cancel is the termination path.
  Controller.dispatch(MenuEvent{EventKind::kBack});
  finishDispatch();
}

bool GameUiModule::connectionConnectAddress(const char *address,
                                            std::string &errorToken) {
  std::string host;
  int port = 0;
  if (!address || !*address ||
      !services::loading::LoadingModel::parseAddress(address, host, port)) {
    errorToken = "#Loading_AddressInvalid";
    return false; // nothing reaches the engine unvalidated
  }
  // A user-initiated attempt starts a fresh auto-retry chain.
  AutoRetry.reset();
  Controller.dispatch(gameui::menu::makeConnectRequest(address));
  finishDispatch();
  return true;
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
  } else if (Kind == ActionKind::kDirectConnectConnect ||
             Kind == ActionKind::kPasswordConnect ||
             Kind == ActionKind::kPasswordCancel) {
    // The dialog pages are module documents: their submit/cancel intents
    // travel through the sdk connection section, so nothing arrives here.
    return;
  } else if (Kind == ActionKind::kDirectConnectCancel)
    Controller.dispatch(MenuEvent{EventKind::kBack});
  finishDispatch();
}
} // namespace gameui
