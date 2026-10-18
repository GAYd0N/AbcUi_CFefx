// gameui::VideoFlow implementation - see VideoFlow.h.
#include "VideoFlow.h"

#include "../../../panels/settings/VideoConfirmPage.h"
#include "../../../services/GameUiBridge.h"
#include "../../../services/settings/VideoModeModel.h"
#include "modules/gameui/engine/VideoBindings.h"

#include <abc/Console.h>

namespace gameui {

VideoFlow::VideoFlow(VideoHost host, services::settings::VideoModeModel &model)
    : Host(std::move(host)), Model(model) {}

void VideoFlow::setPage(panels::settings::VideoConfirmPage *page) {
  Page = page;
}

void VideoFlow::shutdown() {
  Page = nullptr;
  ConfirmPresented = false;
  CountdownShown = -1;
}

void VideoFlow::resolveRecovery() {
  const auto outcome = Model.resolveRecovery();
  switch (outcome) {
  case services::settings::VideoModeModel::RecoveryResult::kPending:
    abc::ConsoleWarn(
        "[AbcUi] display change awaiting confirmation (%s)\n",
        services::settings::VideoModeModel::describe(Model.actual()).c_str());
    break;
  case services::settings::VideoModeModel::RecoveryResult::kAutoReverted:
    abc::ConsoleWarn(
        "[AbcUi] unresolved display change restored to %s\n",
        services::settings::VideoModeModel::describe(Model.actual()).c_str());
    break;
  case services::settings::VideoModeModel::RecoveryResult::kAccepted:
    // Reserved legacy value; the model never infers confirmation from age.
    break;
  case services::settings::VideoModeModel::RecoveryResult::kDeferred:
    abc::ConsoleWarn(
        "[AbcUi] display-change record kept (recovery query/write pending)\n");
    break;
  case services::settings::VideoModeModel::RecoveryResult::kNoRecord:
    break;
  }
  if (Model.awaitingConfirm())
    engine::DeferredVideoConfirmRaise = true; // frame drain raises it
}

void VideoFlow::applyChange() {
  if (!Model.available() || !Model.hasStaged() || Model.awaitingConfirm())
    return;
  if (!Model.apply()) {
    abc::ConsoleWarn(
        "[AbcUi] display switch refused (record or command write failed)\n");
    if (Model.awaitingConfirm())
      engine::DeferredVideoConfirmRaise = true;
    Host.renderSettings();
    return;
  }
  // The restart is queued in the same transaction (deferred thunk) and the
  // confirmation raise rides the frame drain: never from here.
  engine::DeferredVideoConfirmRaise = true;
  Host.renderSettings();
}

void VideoFlow::raiseConfirmation() {
  if (!Page || !Host.context())
    return;
  // The takeover rule: the confirmation outranks everything else in the
  // menu (it is the only way back from a bad mode). If the menu was not up,
  // raise it through the bridge - the takeover keeps the original UI
  // suppressed.
  if (!Host.menuPresented())
    services::nativeUiBridge().requestMenuUp(services::NativeUiOrigin::kOwnUi);
  // The page covers the whole viewport: hide the other documents while it is
  // up (the sync re-shows the right one when it closes).
  Host.hideMenuDocuments();
  ConfirmPresented = true;
  CountdownShown = Model.remainingSeconds();
  Page->render();
  Page->show();
}

void VideoFlow::closeConfirmation() {
  if (Page)
    Page->hide();
  ConfirmPresented = false;
  CountdownShown = -1;
  if (Host.menuPresented())
    Host.syncMenuPresentation();
}

void VideoFlow::syncConfirmPage() {
  if (!Page)
    return;
  if (Model.awaitingConfirm()) {
    // Never raise inside a frame that issued engine commands: queue it for
    // the frame drain (see DeferredVideoConfirmRaise).
    if (!ConfirmPresented || !Page->isVisible())
      engine::DeferredVideoConfirmRaise = true;
  } else if (ConfirmPresented) {
    closeConfirmation();
  }
}

void VideoFlow::tickTimeout() {
  if (!Page || !ConfirmPresented)
    return;
  const int seconds = Model.remainingSeconds();
  if (seconds == CountdownShown)
    return;
  CountdownShown = seconds;
  Page->render();
}

void VideoFlow::handleAction(navigation::ActionKind kind) {
  if (kind == navigation::ActionKind::kVideoApply) {
    applyChange();
    return;
  }
  if (kind == navigation::ActionKind::kVideoKeep) {
    // Keep the confirmed mode; a refused confirmation stays put (the page
    // re-renders the current truth).
    if (Model.confirm()) {
      abc::ConsolePrint(
          "[AbcUi] display change confirmed (%s)\n",
          services::settings::VideoModeModel::describe(Model.actual()).c_str());
      syncConfirmPage();
    } else if (Page) {
      Page->render();
    }
    return;
  }
  if (kind == navigation::ActionKind::kVideoRevert) {
    // Restore the previous mode; a refused write keeps the dialog up (the
    // deadline retries every second, the record survives).
    if (Model.revert()) {
      abc::ConsolePrint(
          "[AbcUi] display change reverted to %s\n",
          services::settings::VideoModeModel::describe(Model.actual()).c_str());
      syncConfirmPage();
    } else if (Page) {
      Page->render();
    }
  }
}

} // namespace gameui
