#pragma once

// gameui::CreateGameFlow - the create-local-game model, map catalog and the
// kept (pending) request used by local retry.
//
// The page became a module (abcui.gameui, sdk creategame section): the flow
// keeps the model, the server options and map catalog feeds, the kept
// pending request and the apply boundary (the page submits the form's
// fields once on Apply; this assembles, validates and dispatches them).
// The composition root supplies the typed controller dispatches and the
// auto-retry reset; the engine launch path is a thin method so
// GameUiModule::launchLocalGame can stay a static backend shim.

#include "../../../services/creategame/CreateGameModel.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace gameui {

struct CreateGameHost {
  std::function<void(const std::string &map)> dispatchCreate;
  std::function<void()> finishDispatch;
  std::function<void()> resetAutoRetry;
};

class CreateGameFlow {
public:
  CreateGameFlow(CreateGameHost host,
                 services::creategame::CreateGameModel::Seams seams);

  void shutdown();

  void setOptions(std::vector<services::creategame::ServerOption> options);
  bool optionsEmpty() const;
  void loadCatalog();
  // The page's Apply: assembles the request from the form fields (fixed
  // names "map"/"players"/"name"/"password"/"lan" plus one per option),
  // validates, keeps the pending request and dispatches the attempt.
  // Returns 0 on refusal with a hint token/text.
  bool apply(const std::vector<std::pair<std::string, std::string>> &fields,
             std::string &hint);

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
  services::creategame::Request Pending;
  bool PendingValid = false;
};

} // namespace gameui
