// Extracted from GameUiModule.cpp (UTF-8). Responsibility: lifecycle/Frame.
#include "modules/gameui/GameUiModule.h"
#include <abc/CvarCmd.h>
#include "modules/gameui/engine/VideoBindings.h"
namespace gameui {
using namespace engine;
void GameUiModule::pumpBackgroundWork() {
  // T14: the engine's loading stream is drained first and independently
  // of menu presentation - the controller transitions (attempt start,
  // loading, success/failure/password) must not wait for Rml readiness,
  // and the drain is the ONLY consumer of the copied feed.
  drainLoadingFeed();
  // T17: the server-query drain runs every frame on this (main) thread
  // and independently of menu presentation. Provider callbacks arrive
  // from the engine's SteamAPI_RunCallbacks pump and land in the
  // provider's bounded queue; this is the only consumer, and the
  // query deadline is enforced here (bounded per-frame work). The
  // status-edge log rides the same beat.
  ServerBrowser.Query.pump();
  Server->logQueryChange();
  // 自动重试: the connection-error overlay's bounded auto-retry beat (the
  // browser page's toggle is the enable bit). Runs before the page
  // projections so a fired retry re-projects through the same frame.
  tickAutoRetry();
  // T12: drain the deferred video restart FIRST and independently of
  // menu presentation - the transaction owns it, not the page (see the
  // DeferredVideoRestart note above).
  const bool restartDrained = DeferredVideoRestart;
  if (DeferredVideoRestart) {
    DeferredVideoRestart = false;
    abc::EngineClientCmd("_restart\n");
  }
  // The queued confirmation raise never shares a frame with a restart
  // drain (see the DeferredVideoConfirmRaise note): one frame apart.
  if (!restartDrained && DeferredVideoConfirmRaise && Context) {
    DeferredVideoConfirmRaise = false;
    Video->raiseConfirmation();
  }
  // T12 confirmation beat: the monotonic deadline runs on real time.
  // The tick fires the auto-revert even when the window is hidden or
  // unfocused - the deadline never extends. A revert/auto-resolve
  // re-projects the dialog (or closes it) once.
  if (VideoModel.awaitingConfirm()) {
    const bool reverted = VideoModel.tick();
    Video->tickTimeout();
    if (reverted || !VideoModel.awaitingConfirm())
      Video->syncConfirmPage();
  }
}
} // namespace gameui
