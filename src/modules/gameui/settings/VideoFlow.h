#pragma once

// gameui::VideoFlow - the display-mode confirmation flow.
//
// Owns the confirmation page state (visible/countdown) and the recovery /
// apply / confirm / revert / timeout transitions. It deliberately does not
// include GameUiModule.h: the composition root supplies a small callback
// struct, so the flow can be moved or tested without the whole god object.

#include "../navigation/MenuAction.h"

#include <functional>

namespace Rml {
class Context;
}
namespace panels::settings {
class VideoConfirmPage;
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
  std::function<void()> renderSettings;
};

class VideoFlow {
public:
  VideoFlow(VideoHost host, services::settings::VideoModeModel &model);

  // The page is created with the other documents at context-ready; null
  // before that and after shutdown.
  void setPage(panels::settings::VideoConfirmPage *page);
  void shutdown();
  bool confirmPresented() const {
    return ConfirmPresented;
  }

  void resolveRecovery();
  void applyChange();
  void raiseConfirmation();
  void closeConfirmation();
  void syncConfirmPage();
  void tickTimeout();
  void handleAction(navigation::ActionKind kind);

private:
  VideoHost Host;
  services::settings::VideoModeModel &Model;
  panels::settings::VideoConfirmPage *Page = nullptr;
  bool ConfirmPresented = false;
  int CountdownShown = -1;
};

} // namespace gameui
