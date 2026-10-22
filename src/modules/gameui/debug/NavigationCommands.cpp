// Debug-only navigation commands; extracted without changing command names.
#ifndef NDEBUG
#include <metahook.h>

#include "modules/gameui/GameUiModule.h"
#include "DebugCommands.h"
#include <abc/Console.h>
namespace gameui {
void DebugCommands::menuPageCommand() {
  if (!currentModule() || !currentModule()->Context) {
    abc::ConsoleWarn("[AbcUi] menu not available\n");
    return;
  }
  auto &bridge = services::nativeUiBridge();
  if (currentModule()->MenuPresented) {
    bridge.requestMenuDown(services::NativeUiOrigin::kOwnUi);
    abc::ConsolePrint("[AbcUi] menu hidden (bridge menuUp=%d)\n",
                      bridge.menuUp() ? 1 : 0);
    return;
  }
  // Diagnostic raise: the same sink path the takeover drives (this is
  // the T05/T06 dev seam, not a second truth).
  bridge.requestMenuUp(services::NativeUiOrigin::kOwnUi);
  abc::ConsolePrint("[AbcUi] menu shown (bridge menuUp=%d)\n",
                    bridge.menuUp() ? 1 : 0);
}

// abc_ui_menu_model - the character card's preview view moved into the
// abcui.gameui module with its page (the DOM is module-owned); the command
// remains registered so old calibration scripts fail with a clear note.
void DebugCommands::menuModelCommand() {
  abc::ConsoleWarn("[AbcUi] menu model: the home page is module-owned; the "
                   "preview element is no longer reachable from the console\n");
}

// abc_ui_friends_test [clear | add <name> <address> | open <index>] -
// Debug-only seam for the friends board: it swaps the Steam provider for a
// scripted one (the same tactic the server-query tests use), opens a row's
// context menu, or clears the injection.
void DebugCommands::friendsTestCommand() {
  namespace friends = services::friends;
  // One scripted provider for the whole session; the entries are the ones
  // the command injected.
  static struct Scripted final : friends::IFriendsProvider {
    std::vector<friends::FriendEntry> Rows;
    bool available() override {
      return true;
    }
    int count() override {
      return static_cast<int>(Rows.size());
    }
    bool entry(int index, friends::FriendEntry &out) override {
      if (index < 0 || index >= static_cast<int>(Rows.size()))
        return false;
      out = Rows[index];
      return true;
    }
  } Scripted;
  static bool Injected = false;

  const std::string action =
      gEngfuncs.Cmd_Argc() > 1 ? gEngfuncs.Cmd_Argv(1) : "clear";
  if (action == "clear") {
    Scripted.Rows.clear();
    friends::friendsModel().setProvider(friends::steamFriendsProvider());
    Injected = false;
    abc::ConsolePrint("[AbcUi] friends test: steam provider restored\n");
    return;
  }
  if (action == "add" && gEngfuncs.Cmd_Argc() >= 4) {
    if (!Injected) {
      friends::friendsModel().setProvider(&Scripted);
      Injected = true;
    }
    friends::FriendEntry entry;
    entry.SteamId = 76561190000000000ull + Scripted.Rows.size();
    entry.Name = gEngfuncs.Cmd_Argv(2);
    entry.Game = "Sven Co-op";
    entry.Connect = gEngfuncs.Cmd_Argv(3);
    entry.InGame = true;
    Scripted.Rows.push_back(entry);
    friends::friendsModel().invalidate();
    abc::ConsolePrint("[AbcUi] friends test: injected '%s' at %s\n",
                      entry.Name.c_str(), entry.Connect.c_str());
    return;
  }
  if (action == "open" || action == "join") {
    // The row DOM and its context menu moved into the abcui.gameui module
    // with the shell page; injected rows still project (the module reads
    // the model through the sdk social section), but the menu cannot be
    // poked from the console anymore.
    abc::ConsoleWarn("[AbcUi] friends test: '%s' needs the module page's DOM "
                     "and is no longer available\n",
                     action.c_str());
    return;
  }
  abc::ConsolePrint("[AbcUi] friends test: clear | add <name> <address>\n");
}

// abc_ui_settings_page <pageId> - Debug-only: open one settings page without
// mouse navigation. Lists the available ids when called without arguments.
void DebugCommands::settingsPageCommand() {
  GameUiModule *module = currentModule();
  if (!module) {
    abc::ConsoleWarn("[AbcUi] settings page not available\n");
    return;
  }
  if (gEngfuncs.Cmd_Argc() < 2) {
    for (const auto &page : module->UiConfig.pages())
      abc::ConsolePrint("[AbcUi] settings page: %s\n", page.Id.c_str());
    return;
  }
  // The settings page is a module document now: its tab selection is
  // module-owned and cannot be driven from the console.
  abc::ConsoleWarn("[AbcUi] settings page '%s': the page module owns the tab "
                   "selection\n",
                   gEngfuncs.Cmd_Argv(1));
}

} // namespace gameui
#endif
