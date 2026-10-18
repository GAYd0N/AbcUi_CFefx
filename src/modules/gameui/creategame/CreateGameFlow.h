#pragma once

// gameui::CreateGameFlow - the create-local-game page, map catalog and the
// kept (pending) request used by local retry.
//
// Owns CreateGameModel, the pending Request and the page latch. The
// composition root supplies the typed controller dispatches and the
// auto-retry reset; the engine launch path is a thin method so
// GameUiModule::launchLocalGame can stay a static backend shim.

#include "../../../services/creategame/CreateGameModel.h"
#include "../navigation/MenuAction.h"

#include <functional>
#include <string>
#include <vector>

namespace panels::creategame {
class CreateGamePage;
}

namespace gameui {

struct CreateGameHost {
  std::function<void(const std::string &map)> dispatchCreate;
  std::function<void()> dispatchBack;
  std::function<void()> finishDispatch;
  std::function<void()> resetAutoRetry;
};

class CreateGameFlow {
public:
  CreateGameFlow(CreateGameHost host,
                 services::creategame::CreateGameModel::Seams seams);

  void setPage(panels::creategame::CreateGamePage *page);
  void shutdown();
  bool presented() const {
    return Presented;
  }
  void setPresented(bool presented) {
    Presented = presented;
  }
  void hide();

  void setOptions(std::vector<services::creategame::ServerOption> options);
  bool optionsEmpty() const;
  void loadCatalog();
  void present();
  void apply();
  void handleAction(navigation::ActionKind kind);

  // Local-retry seam: replay the kept request through the controller.
  bool replayPending();
  void invalidatePending();
  const services::creategame::Request &pending() const {
    return Pending;
  }
  bool pendingValid() const {
    return PendingValid;
  }
  std::vector<std::string> settlePendingOptions();

  // Engine launch path used by the module's launchLocalGame shim.
  bool launchLocal(const char *map, const char **reasonToken);

  // Debug probe surface.
  services::creategame::CreateGameModel &model() {
    return Model;
  }

private:
  static const char *
  hintTokenFor(const services::creategame::Validation &validation);

  CreateGameHost Host;
  services::creategame::CreateGameModel Model;
  panels::creategame::CreateGamePage *Page = nullptr;
  services::creategame::Request Pending;
  bool PendingValid = false;
  bool Presented = false;
};

} // namespace gameui
