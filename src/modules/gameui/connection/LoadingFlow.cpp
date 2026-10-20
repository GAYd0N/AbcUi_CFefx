// Extracted from GameUiModule.cpp (UTF-8). Responsibility:
// connection/LoadingFlow.
#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/GameUiPages.h"
#include <string>
#include <abc/Console.h>
namespace gameui {
void GameUiModule::syncAttemptState() {
  const auto &state = Controller.state();
  // Connecting starts a remote/local attempt; Loading also starts the model
  // for an engine-initiated changelevel (kMapLoad), whose generation is
  // allocated by the controller on the kMapLoadStarted transition.
  if ((state.phase == gameui::menu::BackendPhase::kConnecting ||
       state.phase == gameui::menu::BackendPhase::kLoading) &&
      state.connectGeneration != 0 &&
      LoadingModelState.generation() != state.connectGeneration) {
    LoadingModelState.startAttempt(state.connectGeneration,
                                   state.connectAddress.c_str());
    // T19: which kind this generation came from (the loading page
    // title); the controller retires its own attemptKind with the
    // generation, so capture it while it is still set.
    CurrentAttemptIsLocal =
        state.attemptKind == gameui::menu::AttemptKind::kLocalGame;
    if (state.attemptKind == gameui::menu::AttemptKind::kConnect)
      Create->invalidatePending(); // a stale local request can never retry
    PasswordInvalid = false;
    PasswordPrompts = 0;
    PasswordReprepare = false;
  }
  if (LoadingModelState.generation() != 0 &&
      LoadingModelState.generation() != state.connectGeneration) {
    // The attempt ended (cancel/success/failure): the stream of the
    // retired generation keeps arriving and is dropped by the model's
    // generation filter from here on.
    LoadingModelState.retire();
    // T19: remember the ended attempt's kind for the error overlay's
    // retry (the controller's own attemptKind is already retired).
    LastAttemptWasLocal = CurrentAttemptIsLocal;
    CurrentAttemptIsLocal = false;
    PasswordInvalid = false;
    PasswordPrompts = 0;
    PasswordReprepare = false;
    // T18b: an attempt that ends without the join beat retires the
    // pending history entry unrecorded (only a real join writes it).
    Server->discardPendingHistory();
  }
}

void GameUiModule::drainLoadingFeed() {
  services::LoadingFeedEvent event;
  bool changed = false;
  bool rendered = false;
  while (services::loadingFeed().pop(event)) {
    changed = true;
    // A server changelevel streams IGameUI loading events with no connect
    // attempt behind them. Turning the level-start event into the ordinary
    // attempt machinery is what keeps the Loading page (and the menu host)
    // up: without it the phase stayed InGame, DisconnectFromServer had
    // already cleared the level fact, and the next ActivateGameUI reconciled
    // to Disconnected - the no-map Main page and its menu movie appeared
    // while the new map was still loading. A live attempt
    // (Connecting/Loading) owns its stream; emergency/terminal screens are
    // left alone. Only "level" starts a transition, so a stray resource
    // event can never open a loading page that no finish would close.
    const auto phase = Controller.state().phase;
    if (event.kind == services::LoadingFeedKind::kLoadingStarted &&
        event.text == "level" &&
        phase != gameui::menu::BackendPhase::kConnecting &&
        phase != gameui::menu::BackendPhase::kLoading &&
        phase != gameui::menu::BackendPhase::kFatal &&
        phase != gameui::menu::BackendPhase::kShuttingDown) {
      Controller.dispatch(gameui::menu::makeMapLoadStarted());
    }
    syncAttemptState();
    const uint32_t live = LoadingModelState.generation();
    switch (event.kind) {
    case services::LoadingFeedKind::kLoadingStarted:
      LoadingModelState.loadingStarted(live, event.text.c_str(),
                                       event.text2.c_str());
      break;
    case services::LoadingFeedKind::kLoadingFinished:
      LoadingModelState.loadingFinished(live, event.text.c_str());
      break;
    case services::LoadingFeedKind::kStartProgress:
      LoadingModelState.startProgress(live, event.text.c_str(), event.steps);
      break;
    case services::LoadingFeedKind::kContinueProgress:
      LoadingModelState.continueProgress(live, event.point, event.fraction);
      break;
    case services::LoadingFeedKind::kStopProgress:
      LoadingModelState.stopProgress(live, event.error);
      break;
    case services::LoadingFeedKind::kStatusText:
      LoadingModelState.setStatusText(live, event.text.c_str());
      break;
    case services::LoadingFeedKind::kSecondaryProgress:
      LoadingModelState.setSecondaryProgress(live, event.fraction);
      break;
    case services::LoadingFeedKind::kSecondaryText:
      LoadingModelState.setSecondaryProgressText(live, event.text.c_str());
      break;
    case services::LoadingFeedKind::kPasswordPrompt:
      // The engine asked for a server password (IGameUI +0x58).
      // The first prompt of a generation = "required"; any later
      // one = the previous password was rejected (the engine's own
      // flag polarity is not pinned - the per-generation count is
      // the authoritative signal for the lead line).
      if (live != 0) {
        PasswordInvalid = PasswordPrompts > 0;
        ++PasswordPrompts;
        PasswordReprepare = true; // re-prepare clears the field
      }
      break;
    case services::LoadingFeedKind::kCount:
      break;
    }
    // The classification is the single decision table (pinned by
    // abc_ui_loading_model): value-only events never transition.
    switch (services::classifyLoadingEvent(event)) {
    case services::LoadingStep::kNone:
      break;
    case services::LoadingStep::kStarted:
      if (live != 0 &&
          Controller.state().phase == gameui::menu::BackendPhase::kConnecting)
        Controller.dispatch(gameui::menu::makeLoadingStarted(live));
      break;
    case services::LoadingStep::kJoined:
      if (live != 0) {
        // T18b: the engine-confirmed join is the history moment
        // (the shipped ConnectedToGame wrote flags=2 into Steam's
        // store exactly here).
        Server->recordPendingHistory();
        // A confirmed join ends any auto-retry chain.
        AutoRetry.reset();
        if (CurrentAttemptIsLocal) {
          for (const auto &Name : Create->settlePendingOptions())
            abc::ConsoleWarn("[AbcUi] local server option did not apply: %s\n",
                             Name.c_str());
        }
        // T19: a confirmed local join consumes the kept request -
        // nothing left to retry once the server is up.
        Create->invalidatePending();
        Controller.dispatch(gameui::menu::makeLoadingFinished(
            live, gameui::menu::LoadingOutcome::kSuccess, nullptr));
      }
      break;
    case services::LoadingStep::kFailed: {
      if (live == 0)
        break;
      const std::string &reason = event.text.empty() ? event.text2 : event.text;
      Controller.dispatch(gameui::menu::makeLoadingFinished(
          live, gameui::menu::LoadingOutcome::kFailed, reason.c_str()));
      break;
    }
    case services::LoadingStep::kPasswordRequired:
      if (live != 0)
        Controller.dispatch(gameui::menu::makeLoadingFinished(
            live, gameui::menu::LoadingOutcome::kPasswordRequired, nullptr));
      break;
    }
    // The visible loading page re-projects once per drained batch
    // (values moved); never inside the pop loop's per-event churn.
    if (!rendered && MenuPresented && LoadingVisible && Loading) {
      Loading->render();
      rendered = true;
    }
  }
  if (!changed)
    return;
  syncAttemptState();
  // An engine-initiated map load has no menu page behind it: the player was
  // in game with the menu closed. Raise the host now so the Loading page has
  // a surface (the engine may or may not call ActivateGameUI for the load).
  // A normal connect already has the menu up and skips this.
  if (!MenuPresented &&
      Controller.state().phase == gameui::menu::BackendPhase::kLoading) {
    if (!services::nativeUiBridge().menuUp())
      services::nativeUiBridge().requestMenuUp(
          services::NativeUiOrigin::kOwnUi);
    if (!MenuPresented)
      presentMenu();
  }
  if (MenuPresented)
    syncMenuPresentation();
  // A drained success/failure moved the controller; a no-longer-active
  // menu lowers the bridge exactly like a decided close (T14: the
  // loading page disappears on success through the same path).
  if (!Controller.isMenuActive() && services::nativeUiBridge().menuUp())
    services::nativeUiBridge().requestMenuDown(
        services::NativeUiOrigin::kOwnUi);
}

} // namespace gameui
