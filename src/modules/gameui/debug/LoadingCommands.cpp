// Debug-only loading commands; extracted without changing command names.
#ifndef NDEBUG
#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/GameUiPages.h"
#include "DebugCommands.h"
#include <abc/Console.h>
#include <abc/Plugin.h>
#include "rmlui/VGUI2ExtensionImport.h"
#include <string>
namespace gameui {
void DebugCommands::loadingConnectCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  if (gEngfuncs.Cmd_Argc() < 2) {
    abc::ConsoleWarn("[AbcUi] usage: abc_ui_loading_connect <host[:port]>\n");
    return;
  }
  const std::string address = gEngfuncs.Cmd_Argv(1);
  std::string host;
  int port = 0;
  if (!services::loading::LoadingModel::parseAddress(address.c_str(), host,
                                                     port)) {
    abc::ConsoleWarn(
        "[AbcUi] loading connect refused (invalid address \"%s\")\n",
        address.c_str());
    return;
  }
  self.Controller.dispatch(gameui::menu::makeConnectRequest(address.c_str()));
  self.finishDispatch();
  abc::ConsolePrint("[AbcUi] loading connect issued: \"%s\" gen=%u phase=%d\n",
                    address.c_str(), self.LoadingModelState.generation(),
                    static_cast<int>(self.Controller.state().phase));
}

void DebugCommands::loadingCancelCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  const uint32_t live = self.LoadingModelState.generation();
  if (live != 0 && !self.LoadingModelState.requestCancel(live)) {
    abc::ConsoleWarn("[AbcUi] loading cancel already latched (gen=%u)\n", live);
    return;
  }
  self.Controller.dispatch(MenuEvent{EventKind::kConnectCancel});
  self.finishDispatch();
  abc::ConsolePrint(
      "[AbcUi] loading cancel issued (previous gen=%u, phase=%d)\n", live,
      static_cast<int>(self.Controller.state().phase));
}

void DebugCommands::loadingPasswordCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  if (gEngfuncs.Cmd_Argc() < 2) {
    abc::ConsoleWarn("[AbcUi] usage: abc_ui_loading_password <password>\n");
    return;
  }
  if (!self.Controller.state().overlays.contains(MenuOverlay::kPassword)) {
    abc::ConsoleWarn("[AbcUi] no password overlay is up\n");
    return;
  }
  self.Controller.dispatch(
      gameui::menu::makePasswordSubmitted(gEngfuncs.Cmd_Argv(1)));
  self.finishDispatch();
  abc::ConsolePrint("[AbcUi] loading password submitted (gen=%u)\n",
                    self.Controller.state().connectGeneration);
}

void DebugCommands::loadingStatusCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  const auto &state = self.Controller.state();
  abc::ConsolePrint(
      "[AbcUi] loading: phase=%d gen=%u modelGen=%u canceled=%d addr=\"%s\"\n",
      static_cast<int>(state.phase), state.connectGeneration,
      self.LoadingModelState.generation(),
      self.LoadingModelState.canceled() ? 1 : 0,
      self.LoadingModelState.address().c_str());
  abc::ConsolePrint("[AbcUi] loading: pages=%d overlays=%d menuPresented=%d "
                    "loadingVisible=%d\n",
                    static_cast<int>(state.pages.size()),
                    static_cast<int>(state.overlays.size()),
                    self.MenuPresented ? 1 : 0, self.LoadingVisible ? 1 : 0);
  {
    std::string stack = "pages=[";
    for (const MenuPage page : state.pages)
      stack += std::to_string(static_cast<int>(page)) + " ";
    stack += "] overlays=[";
    for (const MenuOverlay overlay : state.overlays)
      stack += std::to_string(static_cast<int>(overlay)) + " ";
    stack += "]";
    abc::ConsolePrint("[AbcUi] loading: %s\n", stack.c_str());
  }
  abc::ConsolePrint("[AbcUi] loading: primary known=%d point=%d steps=%d "
                    "frac=%.3f status=\"%s\"\n",
                    self.LoadingModelState.primary().Known ? 1 : 0,
                    self.LoadingModelState.primary().Point,
                    self.LoadingModelState.primary().Steps,
                    self.LoadingModelState.primary().Fraction,
                    self.LoadingModelState.statusText().c_str());
  abc::ConsolePrint("[AbcUi] loading: secondary frac=%.3f text=\"%s\" "
                    "resource=\"%s\" errorReason=\"%s\"\n",
                    self.LoadingModelState.secondaryFraction(),
                    self.LoadingModelState.secondaryText().c_str(),
                    self.LoadingModelState.resourceText().c_str(),
                    state.errorReason.c_str());
  abc::ConsolePrint("[AbcUi] loading: hasLevel=%d feedDropped=%u "
                    "passwordInvalid=%d prompts=%d\n",
                    services::nativeUiBridge().hasLevel() ? 1 : 0,
                    services::loadingFeed().dropped(),
                    self.PasswordInvalid ? 1 : 0, self.PasswordPrompts);
  abc::ConsolePrint("[AbcUi] loading: hookSlot22=%d\n",
                    vgui2ext::gameUiLoadingHookInstalled() ? 1 : 0);
}

} // namespace gameui
#endif
