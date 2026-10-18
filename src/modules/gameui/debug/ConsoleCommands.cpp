// Debug-only console commands; extracted without changing command names.
#ifndef NDEBUG
#include <metahook.h>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/GameUiPages.h"
#include "DebugCommands.h"
#include <abc/Console.h>
#include "services/console/ConsoleBackend.h"
namespace gameui {
void DebugCommands::consoleUiStatusCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  auto &bridge = services::nativeUiBridge();
  int rows = -1;
  int scrollTop = -1;
  int scrollHeight = -1;
  if (self.Console && self.Console->document()) {
    if (Rml::Element *history =
            self.Console->document()->GetElementById("console-history")) {
      rows = history->GetNumChildren();
      scrollTop = static_cast<int>(history->GetScrollTop());
      scrollHeight = static_cast<int>(history->GetScrollHeight());
    }
  }
  abc::ConsolePrint("[AbcUi] console-ui: requested=%d visible=%d page=%d "
                    "menu=%d rows=%d scrollTop=%d scrollHeight=%d\n",
                    bridge.consoleRequested() ? 1 : 0,
                    bridge.consoleVisible() ? 1 : 0,
                    self.ConsolePresented ? 1 : 0, self.MenuPresented ? 1 : 0,
                    rows, scrollTop, scrollHeight);
  // Completion catalog probe (WS6): what the dropdown would show for "sv".
  {
    const auto matches = services::console::consoleBackend().complete("sv", 12);
    abc::ConsolePrint("[AbcUi] console-ui: complete('sv') -> %u matches",
                      static_cast<unsigned>(matches.size()));
    for (std::size_t i = 0; i < matches.size() && i < 12; ++i)
      abc::ConsolePrint(" %s", matches[i].c_str());
    abc::ConsolePrint("\n");
    if (Rml::ElementDocument *document =
            self.Console ? self.Console->document() : nullptr) {
      // The acceptance readback: the editor value plus its caret
      // [start,end]. Typed text parks the caret at the end; a
      // caret-moving key (LEFT/RIGHT) must move it, and Backspace then
      // edits at that spot - effects a keyboard-injection harness can
      // assert without a text-input stream.
      int caretStart = -1;
      int caretEnd = -1;
      Rml::String editorValue = "<none>";
      if (auto *input = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
              document->GetElementById("console-input"))) {
        editorValue = input->GetValue();
        input->GetSelection(&caretStart, &caretEnd, nullptr);
      }
      abc::ConsolePrint("[AbcUi] console-ui: editor value='%s' caret=[%d,%d]\n",
                        editorValue.c_str(), caretStart, caretEnd);
    }
    if (gEngfuncs.Cmd_Argc() >= 2 && self.Console)
      self.Console->debugType(gEngfuncs.Cmd_Argv(1));
    if (Rml::ElementDocument *document =
            self.Console ? self.Console->document() : nullptr) {
      if (Rml::Element *list =
              document->GetElementById("console-suggestions")) {
        abc::ConsolePrint("[AbcUi] console-ui: dropdown rows=%d\n",
                          list->GetNumChildren());
        for (int i = 0; i < list->GetNumChildren() && i < 4; ++i)
          abc::ConsolePrint("[AbcUi] console-ui: row[%d]='%s'\n", i,
                            list->GetChild(i)->GetInnerRML().c_str());
      }
    }
  }
}

void DebugCommands::consoleUiToggleCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  currentModule()->onMenuAction("console");
  consoleUiStatusCommand();
}

// The page's own submission chain, driven without an injectable keyboard:
// type the line through the editor seam, then press Enter through the real
// context key path (the editor is the focused control). Everything after
// that is production: the page's Enter handler, the submit sink, the T15
// backend and the module's unknown-verb answer.
void DebugCommands::consoleUiSubmitCommand() {
  if (!currentModule() || !currentModule()->Console ||
      !currentModule()->Console->document()) {
    abc::ConsoleWarn("[AbcUi] console page not open\n");
    return;
  }
  if (gEngfuncs.Cmd_Argc() < 2) {
    abc::ConsoleWarn("[AbcUi] usage: abc_ui_console_ui_submit <command...>\n");
    return;
  }
  GameUiModule &self = *currentModule();
  std::string command;
  for (int i = 1; i < gEngfuncs.Cmd_Argc(); ++i) {
    if (i > 1)
      command += ' ';
    command += gEngfuncs.Cmd_Argv(i);
  }
  self.Console->debugType(command.c_str());
  if (self.Context) {
    self.Context->ProcessKeyDown(Rml::Input::KI_RETURN, 0);
    self.Context->ProcessKeyUp(Rml::Input::KI_RETURN, 0);
  }
  abc::ConsolePrint("[AbcUi] console-ui submit issued: \"%s\"\n",
                    command.c_str());
}

} // namespace gameui
#endif
