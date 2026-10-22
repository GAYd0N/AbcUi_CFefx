// Debug-only creategame commands; extracted without changing command names.
#ifndef NDEBUG
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include "modules/gameui/GameUiModule.h"
#include "DebugCommands.h"
#include <abc/Console.h>
#include <abc/Plugin.h>
namespace gameui {
void DebugCommands::createGameUiCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  // The form DOM moved into the abcui.gameui module; this dumps the
  // framework-side model and the kept request.
  GameUiModule &self = *currentModule();
  const auto &catalog = self.Create->model().catalog();
  abc::ConsolePrint("[AbcUi] create-game-ui: maps=%d first=%s\n",
                    static_cast<int>(catalog.size()),
                    catalog.size() > 0 ? catalog.nameAt(0).c_str() : "(none)");
  abc::ConsolePrint("[AbcUi] create-game-ui: pending=%d map=%s lastLocal=%d\n",
                    self.Create->pendingValid() ? 1 : 0,
                    self.Create->pendingValid()
                        ? self.Create->pending().Map.c_str()
                        : "(none)",
                    self.LastAttemptWasLocal ? 1 : 0);
}

void DebugCommands::createGameFillCommand() {
  // The form fields moved into the abcui.gameui module with its page; a
  // console-driven fill can no longer reach them (apply the real page or
  // use abc_ui_create_game_probe for a direct request).
  abc::ConsoleWarn("[AbcUi] create-game fill: the form is module-owned; use "
                   "abc_ui_create_game_probe for a direct request\n");
}

void DebugCommands::createGameApplyCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  if (gEngfuncs.Cmd_Argc() < 4) {
    abc::ConsoleWarn("[AbcUi] usage: abc_ui_create_game_apply <map> <players> "
                     "<name> [password]\n");
    return;
  }
  // The form DOM moved into the module; this drives the same apply
  // boundary the page's Apply button uses (validate + dispatch).
  GameUiModule &self = *currentModule();
  std::vector<std::pair<std::string, std::string>> fields = {
      {"map", gEngfuncs.Cmd_Argv(1)},
      {"players", gEngfuncs.Cmd_Argv(2)},
      {"name", gEngfuncs.Cmd_Argv(3)},
      {"password", gEngfuncs.Cmd_Argc() >= 5 ? gEngfuncs.Cmd_Argv(4) : ""},
      {"lan", "0"},
  };
  std::string hint;
  const bool ok = self.Create->apply(fields, hint);
  abc::ConsolePrint(
      "[AbcUi] create-game apply: ok=%d hint='%s' phase=%d gen=%u map=%s "
      "pending=%d\n",
      ok ? 1 : 0, hint.c_str(), static_cast<int>(self.Controller.state().phase),
      self.Controller.state().connectGeneration,
      self.Controller.state().connectAddress.c_str(),
      self.Create->pendingValid() ? 1 : 0);
}

void DebugCommands::createGameProbeCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  if (gEngfuncs.Cmd_Argc() < 4) {
    abc::ConsoleWarn("[AbcUi] usage: abc_ui_create_game_probe <map> <players> "
                     "<name> [password]\n");
    return;
  }
  GameUiModule &self = *currentModule();
  services::creategame::Request request;
  request.Map = gEngfuncs.Cmd_Argv(1);
  int players = 0;
  if (!services::creategame::CreateGameModel::parsePlayers(
          gEngfuncs.Cmd_Argv(2), players)) {
    abc::ConsoleWarn("[AbcUi] probe refused: bad players\n");
    return;
  }
  request.MaxPlayers = players;
  request.ServerName = gEngfuncs.Cmd_Argv(3);
  if (gEngfuncs.Cmd_Argc() >= 5)
    request.Password = gEngfuncs.Cmd_Argv(4);
  const auto validation = services::creategame::CreateGameModel::validate(
      self.Create->model().catalog(), request);
  abc::ConsolePrint("[AbcUi] probe validate: ok=%d error=%d field=%d\n",
                    validation.ok() ? 1 : 0, static_cast<int>(validation.error),
                    static_cast<int>(validation.field));
  if (!validation.ok())
    return;
  const auto outcome = self.Create->model().launch(request);
  abc::ConsolePrint(
      "[AbcUi] probe launch: outcome=%d failIdx=%d restoreFailures=%d\n",
      static_cast<int>(outcome), self.Create->model().lastFailIndex(),
      self.Create->model().lastRestoreFailures());
}

} // namespace gameui
#endif
