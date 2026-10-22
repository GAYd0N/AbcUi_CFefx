#pragma once

// gameui::VideoFlow - the display-mode confirmation flow.
//
// Owns the confirmation window's state (visible/countdown) and the recovery /
// apply / confirm / revert / timeout transitions. The dialog itself is a page
// module document (abcui.gameui) presented from the sdk video facts, so the
// flow publishes the page generation and never touches the DOM. It
// deliberately does not include GameUiModule.h: the composition root supplies
// a small callback struct.

#include "../navigation/MenuAction.h"

#include <functional>

namespace Rml {
class Context;
}
namespace services::settings {
class VideoModeModel;
}

namespace gameui {

struct VideoHost {
  std::function<Rml::Context *()> context;
  std::function<bool()> menuPresented;
  std::function<void()> syncMenuPresentation;
  std::function<void()> hideMenuDocuments;
};

class VideoFlow {
public:
  VideoFlow(VideoHost host, services::settings::VideoModeModel &model);

  void shutdown();
  bool confirmPresented() const {
    return ConfirmPresented;
  }

  void resolveRecovery();
  // The restart-driving transaction; true when the batch was issued.
  bool applyChange();
  void raiseConfirmation();
  void closeConfirmation();
  void syncConfirmPage();
  void tickTimeout();
  // The confirmation window's two outcomes (the page reports them through
  // the sdk settings section). True when the transaction took.
  bool confirmKeep();
  bool confirmRevert();
  void handleAction(navigation::ActionKind kind);

private:
  VideoHost Host;
  services::settings::VideoModeModel &Model;
  bool ConfirmPresented = false;
  int CountdownShown = -1;
};

} // namespace gameui
