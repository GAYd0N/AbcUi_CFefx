// Debug-only creategame commands; extracted without changing command names.
#ifndef NDEBUG
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/GameUiPages.h"
#include "DebugCommands.h"
#include <abc/Console.h>
#include <abc/Plugin.h>
namespace gameui {
void DebugCommands::createGameUiCommand() {
  if (!currentModule() || !currentModule()->CreateGamePage_ ||
      !currentModule()->CreateGamePage_->document()) {
    abc::ConsoleWarn("[AbcUi] create-game page not open\n");
    return;
  }
  GameUiModule &self = *currentModule();
  const auto &catalog = self.Create->model().catalog();
  abc::ConsolePrint("[AbcUi] create-game-ui: maps=%d first=%s\n",
                    static_cast<int>(catalog.size()),
                    catalog.size() > 0 ? catalog.nameAt(0).c_str() : "(none)");
  abc::ConsolePrint(
      "[AbcUi] create-game-ui: map='%s' players='%s' name='%s' pw=%d\n",
      self.CreateGamePage_->selectedMap().c_str(),
      self.CreateGamePage_->playersText().c_str(),
      self.CreateGamePage_->serverName().c_str(),
      self.CreateGamePage_->password().empty() ? 0 : 1);
  abc::ConsolePrint("[AbcUi] create-game-ui: pending=%d map=%s lastLocal=%d\n",
                    self.Create->pendingValid() ? 1 : 0,
                    self.Create->pendingValid()
                        ? self.Create->pending().Map.c_str()
                        : "(none)",
                    self.LastAttemptWasLocal ? 1 : 0);
}

void DebugCommands::createGameFillCommand() {
  if (!currentModule() || !currentModule()->CreateGamePage_ ||
      !currentModule()->CreateGamePage_->document()) {
    abc::ConsoleWarn("[AbcUi] create-game page not open\n");
    return;
  }
  if (gEngfuncs.Cmd_Argc() < 4) {
    abc::ConsoleWarn("[AbcUi] usage: abc_ui_create_game_fill <map> <players> "
                     "<name> [password]\n");
    return;
  }
  GameUiModule &self = *currentModule();
  Rml::ElementDocument *doc = self.CreateGamePage_->document();
  if (Rml::ElementFormControlSelect *select =
          rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(
              doc->GetElementById("cg-map"))) {
    // Select by exact value when present, else refuse (the option set
    // is the catalog - a fill cannot invent an entry).
    bool found = false;
    for (int i = 0; i < select->GetNumOptions() && !found; ++i) {
      Rml::Element *option = select->GetOption(i);
      if (option && option->GetAttribute<Rml::String>("value", "") ==
                        gEngfuncs.Cmd_Argv(1)) {
        select->SetSelection(i);
        found = true;
      }
    }
    if (!found)
      abc::ConsoleWarn("[AbcUi] map '%s' is not an enumerated option\n",
                       gEngfuncs.Cmd_Argv(1));
  }
  if (Rml::ElementFormControl *control =
          rmlui_dynamic_cast<Rml::ElementFormControl *>(
              doc->GetElementById("cg-players")))
    control->SetValue(gEngfuncs.Cmd_Argv(2));
  if (Rml::ElementFormControl *control =
          rmlui_dynamic_cast<Rml::ElementFormControl *>(
              doc->GetElementById("cg-name")))
    control->SetValue(gEngfuncs.Cmd_Argv(3));
  if (gEngfuncs.Cmd_Argc() >= 5) {
    if (Rml::ElementFormControl *control =
            rmlui_dynamic_cast<Rml::ElementFormControl *>(
                doc->GetElementById("cg-password")))
      control->SetValue(gEngfuncs.Cmd_Argv(4));
  }
  createGameUiCommand();
}

void DebugCommands::createGameApplyCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  self.Create->apply();
  abc::ConsolePrint(
      "[AbcUi] create-game apply issued: phase=%d gen=%u map=%s pending=%d\n",
      static_cast<int>(self.Controller.state().phase),
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
