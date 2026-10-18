// Debug-only navigation commands; extracted without changing command names.
#ifndef NDEBUG
#include <metahook.h>

#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/GameUiPages.h"
#include "DebugCommands.h"
#include <abc/Console.h>
namespace gameui {
void DebugCommands::menuPageCommand() {
  if (!currentModule() || !currentModule()->Shell ||
      !currentModule()->Context) {
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

// abc_ui_menu_model [<attribute> <value...>] - live tuning of the character
// card's preview view (camera, fov, animation) while the menu is up; with no
// arguments it dumps the current view. Debug-only calibration seam.
void DebugCommands::menuModelCommand() {
  GameUiModule *module = currentModule();
  if (!module || !module->Shell || !module->Context) {
    abc::ConsoleWarn("[AbcUi] menu not available\n");
    return;
  }
  Rml::ElementDocument *document = module->Shell->document();
  Rml::Element *model =
      document ? document->GetElementById("player-model") : nullptr;
  abc::ConsolePrint("[AbcUi] menu facts: presented=%d shellVisible=%d "
                    "context=%d frameTime=%.4f\n",
                    module->MenuPresented ? 1 : 0,
                    module->Shell->isVisible() ? 1 : 0, module->Context ? 1 : 0,
                    module->LastFrameTime);
  if (!model) {
    abc::ConsoleWarn("[AbcUi] player card model element not found\n");
    return;
  }
  // argv(1) = attribute, argv(2..) = value (no subcommand level).
  if (gEngfuncs.Cmd_Argc() >= 3) {
    std::string value = gEngfuncs.Cmd_Argv(2);
    for (int i = 3; i < gEngfuncs.Cmd_Argc(); ++i)
      value += std::string(" ") + gEngfuncs.Cmd_Argv(i);
    model->SetAttribute(gEngfuncs.Cmd_Argv(1), value);
    abc::ConsolePrint("[AbcUi] menu model %s = %s\n", gEngfuncs.Cmd_Argv(1),
                      value.c_str());
    return;
  }
  for (const auto &[name, attr] : model->GetAttributes()) {
    if (name == "src" || name == "id" || name == "class" || name == "style")
      continue;
    abc::ConsolePrint("[AbcUi] menu model %s = %s\n", name.c_str(),
                      attr.Get<Rml::String>().c_str());
  }
  abc::ConsolePrint("[AbcUi] menu model box=%.1fx%.1f abs=(%.1f,%.1f)\n",
                    model->GetOffsetWidth(), model->GetOffsetHeight(),
                    model->GetAbsoluteLeft(), model->GetAbsoluteTop());
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
  if (action == "open" && gEngfuncs.Cmd_Argc() >= 3) {
    GameUiModule *module = currentModule();
    Rml::ElementDocument *document =
        module && module->Shell ? module->Shell->document() : nullptr;
    const int index = atoi(gEngfuncs.Cmd_Argv(2));
    if (!document) {
      abc::ConsoleWarn("[AbcUi] friends test: no menu document\n");
      return;
    }
    Rml::Element *list = document->GetElementById("friends-list");
    Rml::Element *row = list && index >= 0 && index < list->GetNumChildren()
                            ? list->GetChild(index)
                            : nullptr;
    if (!row) {
      abc::ConsoleWarn("[AbcUi] friends test: no row %d\n", index);
      return;
    }
    module->Shell->debugOpenFriendMenu(row, 300, 200);
    abc::ConsolePrint("[AbcUi] friends test: menu opened for row %d\n", index);
    return;
  }
  if (action == "join" && gEngfuncs.Cmd_Argc() >= 3) {
    GameUiModule *module = currentModule();
    if (!module || !module->Shell) {
      abc::ConsoleWarn("[AbcUi] friends test: no menu\n");
      return;
    }
    module->Shell->debugJoinFriend(gEngfuncs.Cmd_Argv(2));
    return;
  }
  abc::ConsolePrint("[AbcUi] friends test: clear | add <name> <address> | open "
                    "<index> | join <steamid>\n");
}

// abc_ui_settings_page <pageId> - Debug-only: open one settings page without
// mouse navigation. Lists the available ids when called without arguments.
void DebugCommands::settingsPageCommand() {
  GameUiModule *module = currentModule();
  if (!module || !module->Settings) {
    abc::ConsoleWarn("[AbcUi] settings page not available\n");
    return;
  }
  if (gEngfuncs.Cmd_Argc() < 2) {
    for (const auto &page : module->UiConfig.pages())
      abc::ConsolePrint("[AbcUi] settings page: %s\n", page.Id.c_str());
    return;
  }
  const bool selected = module->Settings->selectPage(gEngfuncs.Cmd_Argv(1));
  abc::ConsolePrint("[AbcUi] settings page '%s': %s\n", gEngfuncs.Cmd_Argv(1),
                    selected ? "shown" : "unknown");
}

} // namespace gameui
#endif
