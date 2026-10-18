// Debug-only playerlist commands; extracted without changing command names.
#ifndef NDEBUG
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/GameUiPages.h"
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
  if (self.PlayerListPage_ && self.PlayerListPage_->document()) {
    Rml::ElementDocument *doc = self.PlayerListPage_->document();
    Rml::Element *list = doc->GetElementById("pl-list");
    abc::ConsolePrint("[AbcUi] playerlist-ui: doc=%s list=%s rows=%d\n",
                      doc->IsVisible() ? "visible" : "hidden",
                      list ? "found" : "missing",
                      list ? list->GetNumChildren() : -1);
    if (Rml::Element *mute = doc->GetElementById("pl-mute"))
      abc::ConsolePrint("[AbcUi] playerlist-ui: muteLabel='%s' disabled=%d\n",
                        mute->GetInnerRML().c_str(),
                        mute->IsClassSet("pl-disabled") ? 1 : 0);
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
    if (self.PlayerListPage_)
      self.PlayerListPage_->render();
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
  abc::ConsolePrint("[AbcUi] playerlist open: pages=%d presented=%d doc=%s\n",
                    static_cast<int>(self.Controller.state().pages.size()),
                    self.Player->presented() ? 1 : 0,
                    (self.PlayerListPage_ && self.PlayerListPage_->document() &&
                     self.PlayerListPage_->document()->IsVisible())
                        ? "visible"
                        : "hidden");
  playerListUiCommand();
}

void DebugCommands::playerListMuteCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  currentModule()->Player->toggleMute();
  playerListUiCommand();
}

} // namespace gameui
#endif
