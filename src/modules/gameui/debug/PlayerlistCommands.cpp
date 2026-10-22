// Debug-only playerlist commands; extracted without changing command names.
// The page is a module document (abcui.gameui): these commands drive the
// framework-side model only, so the module's projection follows on its own
// menu.paint beat.
#ifndef NDEBUG
#include "modules/gameui/GameUiModule.h"
#include "DebugCommands.h"
#include <abc/Console.h>
#include <abc/Plugin.h>
#include <cstdlib>
namespace gameui {
void DebugCommands::playerListUiCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  self.PlayerList.refresh();
  abc::ConsolePrint(
      "[AbcUi] playerlist: mute=%d gen=%u rows=%d rev=%llu sel=%d\n",
      self.PlayerList.muteAvailable() ? 1 : 0, self.PlayerList.generation(),
      static_cast<int>(self.PlayerList.roster().size()),
      static_cast<unsigned long long>(self.PlayerList.revision()),
      self.PlayerList.selection().valid() ? 1 : 0);
  for (const auto &entry : self.PlayerList.roster()) {
    abc::ConsolePrint("[AbcUi] playerlist row: idx=%d name=\"%s\" steam=%llu "
                      "local=%d bot=%d spec=%d muted=%d\n",
                      entry.index, entry.name.c_str(),
                      static_cast<unsigned long long>(entry.steamId64),
                      entry.local ? 1 : 0, entry.bot ? 1 : 0,
                      entry.spectating ? 1 : 0, entry.muted ? 1 : 0);
  }
}

void DebugCommands::playerListSelectCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  if (gEngfuncs.Cmd_Argc() < 2) {
    abc::ConsoleWarn("[AbcUi] usage: abc_ui_playerlist_select <index>\n");
    return;
  }
  GameUiModule &self = *currentModule();
  const int index = atoi(gEngfuncs.Cmd_Argv(1));
  self.PlayerList.refresh();
  for (const auto &entry : self.PlayerList.roster()) {
    if (entry.index != index)
      continue;
    self.PlayerList.select(services::playerlist::SelectionKey::forEntry(entry));
    abc::ConsolePrint("[AbcUi] playerlist select idx=%d name=\"%s\"\n",
                      entry.index, entry.name.c_str());
    return;
  }
  abc::ConsoleWarn("[AbcUi] playerlist: no player at index %d\n", index);
}

void DebugCommands::playerListOpenCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  if (!self.MenuPresented)
    services::nativeUiBridge().requestMenuUp(services::NativeUiOrigin::kOwnUi);
  self.Controller.dispatch(gameui::menu::makeOpenPage(MenuPage::kPlayerList));
  self.finishDispatch();
  abc::ConsolePrint("[AbcUi] playerlist open: pages=%d page=%d\n",
                    static_cast<int>(self.Controller.state().pages.size()),
                    static_cast<int>(self.Controller.state().pages.top()));
  playerListUiCommand();
}

void DebugCommands::playerListMuteCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  bool mutedAfter = false;
  const auto result = self.PlayerList.toggleMuted(mutedAfter);
  switch (result) {
  case services::playerlist::ToggleError::kNone:
    abc::ConsolePrint("[AbcUi] player mute %s\n",
                      mutedAfter ? "MUTED" : "UNMUTED");
    break;
  case services::playerlist::ToggleError::kUnavailable:
    abc::ConsoleWarn("[AbcUi] player mute unavailable (voice service not "
                     "initialized)\n");
    break;
  case services::playerlist::ToggleError::kNoSelection:
    abc::ConsoleWarn("[AbcUi] player mute refused (no selection)\n");
    break;
  case services::playerlist::ToggleError::kIdentityChanged:
    abc::ConsoleWarn(
        "[AbcUi] player mute refused (selected player left or is a bot)\n");
    break;
  case services::playerlist::ToggleError::kReadbackFailed:
    abc::ConsoleWarn(
        "[AbcUi] player mute refused (backend did not change state)\n");
    break;
  case services::playerlist::ToggleError::kLocalPlayer:
    abc::ConsoleWarn("[AbcUi] player mute refused (local player)\n");
    break;
  case services::playerlist::ToggleError::kStale:
    abc::ConsoleWarn("[AbcUi] player mute refused (stale snapshot)\n");
    break;
  }
  playerListUiCommand();
}

} // namespace gameui
#endif
