// gameui::CreateGameFlow implementation - see CreateGameFlow.h.
#include "CreateGameFlow.h"

#include <abc/Console.h>
#include <abc/CvarCmd.h>
#include <abc/Plugin.h>

#include <utility>

namespace gameui {

CreateGameFlow::CreateGameFlow(
    CreateGameHost host, services::creategame::CreateGameModel::Seams seams)
    : Host(std::move(host)) {
  Model.setSeams(seams);
}

void CreateGameFlow::shutdown() {
  PendingValid = false;
}

void CreateGameFlow::setOptions(
    std::vector<services::creategame::ServerOption> options) {
  Model.setOptions(std::move(options));
}

bool CreateGameFlow::optionsEmpty() const {
  return Model.options().empty();
}

void CreateGameFlow::loadCatalog() {
  services::creategame::MapCatalog catalog;
  if (abc::g_pFileSystem) {
    const char *pathIds[] = {nullptr, "GAMEDOWNLOAD"};
    for (const char *pathId : pathIds) {
      FileFindHandle_t handle = FILESYSTEM_INVALID_FIND_HANDLE;
      const char *name =
          abc::g_pFileSystem->FindFirst("maps/*.bsp", &handle, pathId);
      while (name) {
        catalog.addRawName(name);
        name = abc::g_pFileSystem->FindNext(handle);
      }
      if (handle != FILESYSTEM_INVALID_FIND_HANDLE)
        abc::g_pFileSystem->FindClose(handle);
    }
  } else {
    abc::ConsoleWarn("[AbcUi] map catalog: engine filesystem unavailable\n");
  }
  catalog.finalize();
  Model.setCatalog(std::move(catalog));
#ifndef NDEBUG
  abc::ConsolePrint("[AbcUi] map catalog: %d maps\n",
                    static_cast<int>(Model.catalog().size()));
#endif
}

bool CreateGameFlow::apply(
    const std::vector<std::pair<std::string, std::string>> &fields,
    std::string &hint) {
  services::creategame::Request request;
  bool lan = false;
  std::string playersText;
  for (const auto &field : fields) {
    if (field.first == "map")
      request.Map = field.second;
    else if (field.first == "players")
      playersText = field.second;
    else if (field.first == "name")
      request.ServerName = field.second;
    else if (field.first == "password")
      request.Password = field.second;
    else if (field.first == "lan")
      lan = field.second == "1";
    else
      request.Options.push_back({field.first, field.second});
  }
  request.Lan = lan;

  if (Model.catalog().size() == 0) {
    // No enumerated maps at all (filesystem unavailable): the request cannot
    // be valid, name the real cause.
    hint = "#CG_ErrorUnavailable";
    return false;
  }
  int players = 0;
  if (!services::creategame::CreateGameModel::parsePlayers(playersText.c_str(),
                                                           players)) {
    hint = "#CG_ErrorPlayers";
    return false; // nothing reaches the controller or the engine
  }
  request.MaxPlayers = players;
  if (!Model.validOptions(request)) {
    hint = "#CG_ErrorOptions";
    return false;
  }
  const auto validation =
      services::creategame::CreateGameModel::validate(Model.catalog(), request);
  if (!validation.ok()) {
    hint = hintTokenFor(validation);
    return false; // nothing reaches the controller or the engine
  }
  Pending = request;
  PendingValid = true;
  // A user-initiated local start begins a fresh auto-retry chain.
  Host.resetAutoRetry();
  Host.dispatchCreate(request.Map);
  Host.finishDispatch();
  return true;
}

const char *CreateGameFlow::hintTokenFor(
    const services::creategame::Validation &validation) {
  using services::creategame::Error;
  using services::creategame::Field;
  switch (validation.error) {
  case Error::kPlayersRange:
    return "#CG_ErrorPlayers";
  case Error::kPasswordLength:
  case Error::kPasswordCharset:
    return "#CG_ErrorPassword";
  case Error::kNameEmpty:
  case Error::kNameLength:
  case Error::kNameCharset:
    return "#CG_ErrorName";
  default:
    (void)validation.field;
    return "#CG_ErrorMap";
  }
}

bool CreateGameFlow::replayPending() {
  if (!PendingValid)
    return false;
  Host.dispatchCreate(Pending.Map);
  return true;
}

void CreateGameFlow::invalidatePending() {
  PendingValid = false;
}

std::vector<std::string> CreateGameFlow::settlePendingOptions() {
  std::vector<std::string> failed;
  if (!PendingValid)
    return failed;
  for (const auto &name : Model.settleOptions(Pending))
    failed.push_back(name);
  return failed;
}

bool CreateGameFlow::launchLocal(const char *map, const char **reasonToken) {
  if (!reasonToken)
    return false;
  *reasonToken = "#CG_ErrorInternal";
  if (!PendingValid || !map || Pending.Map != map) {
    abc::ConsoleWarn("[AbcUi] local game launch mismatch (no kept request)\n");
    return false;
  }
  switch (Model.launch(Pending)) {
  case services::creategame::CreateGameModel::Launch::kLaunched:
    return true;
  case services::creategame::CreateGameModel::Launch::kInvalid:
    *reasonToken = "#CG_ErrorInternal"; // validated before the dispatch
    return false;
  case services::creategame::CreateGameModel::Launch::kWriteFailed:
    *reasonToken = "#CG_ErrorWrite";
    return false;
  case services::creategame::CreateGameModel::Launch::kUnwired:
    *reasonToken = "#CG_ErrorInternal";
    return false;
  }
  return false;
}

} // namespace gameui
