// Debug-only video commands; extracted without changing command names.
#ifndef NDEBUG
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/GameUiPages.h"
#include "DebugCommands.h"
#include <abc/Console.h>
#include <abc/Plugin.h>
#include "rmlui/VGUI2ExtensionImport.h"
#include <cstdlib>
namespace gameui {
void DebugCommands::videoStatusCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  abc::ConsolePrint("[AbcUi] video: available=%d\n",
                    self.VideoModel.available() ? 1 : 0);
  services::settings::VideoMode actual;
  const bool haveActual = vgui2ext::currentVideoMode(
      actual.Width, actual.Height, actual.Bpp, actual.Windowed);
  abc::ConsolePrint(
      "[AbcUi] video: current=%d %s\n", haveActual ? 1 : 0,
      haveActual ? services::settings::VideoModeModel::describe(actual).c_str()
                 : "(unreadable)");
  vgui2ext::video::Mode modes[64];
  const int count = vgui2ext::videoModeList(modes, 64);
  abc::ConsolePrint("[AbcUi] video: modes=%d first=%dx%d\n", count,
                    count > 0 ? modes[0].Width : 0,
                    count > 0 ? modes[0].Height : 0);
  self.VideoModel.snapshot();
  abc::ConsolePrint(
      "[AbcUi] video: snapshot actualKnown=%d staged=%d awaiting=%d\n",
      self.VideoModel.actualKnown() ? 1 : 0,
      self.VideoModel.hasStaged() ? 1 : 0,
      self.VideoModel.awaitingConfirm() ? 1 : 0);
}

void DebugCommands::videoScrollCommand() {
  if (!currentModule() || !currentModule()->Settings ||
      !currentModule()->Settings->document()) {
    abc::ConsoleWarn("[AbcUi] settings page not open\n");
    return;
  }
  Rml::Element *panel =
      currentModule()->Settings->document()->GetElementById("panel");
  if (!panel) {
    abc::ConsoleWarn("[AbcUi] settings panel missing\n");
    return;
  }
  panel->SetScrollTop(panel->GetScrollHeight());
  abc::ConsolePrint("[AbcUi] video: panel scrollTop=%.1f height=%.1f\n",
                    panel->GetScrollTop(), panel->GetScrollHeight());
}

void DebugCommands::videoStageCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  self.VideoModel.snapshot();
  // Args: width height (engine argv through the client engine funcs).
  if (gEngfuncs.Cmd_Argc() < 3) {
    abc::ConsoleWarn("[AbcUi] usage: abc_ui_video_stage <width> <height>\n");
    return;
  }
  const int w = atoi(gEngfuncs.Cmd_Argv(1));
  const int h = atoi(gEngfuncs.Cmd_Argv(2));
  if (w <= 0 || h <= 0) {
    abc::ConsoleWarn("[AbcUi] bad dimensions\n");
    return;
  }
  if (!self.VideoModel.stageResolution(w, h)) {
    abc::ConsoleWarn("[AbcUi] stage refused (not an enumerated mode)\n");
    return;
  }
  abc::ConsolePrint("[AbcUi] video staged: %s\n",
                    services::settings::VideoModeModel::describe(
                        self.VideoModel.stagedTarget())
                        .c_str());
  if (self.Settings && self.SettingsView->presented())
    self.Settings->render();
}

void DebugCommands::videoApplyCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  currentModule()->Video->applyChange();
  abc::ConsolePrint("[AbcUi] video apply issued (awaiting=%d)\n",
                    currentModule()->VideoModel.awaitingConfirm() ? 1 : 0);
}

void DebugCommands::videoKeepCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  if (self.VideoModel.confirm()) {
    abc::ConsolePrint(
        "[AbcUi] video kept: %s\n",
        services::settings::VideoModeModel::describe(self.VideoModel.actual())
            .c_str());
    self.Video->syncConfirmPage();
  } else {
    abc::ConsoleWarn("[AbcUi] keep refused (kind=%d)\n",
                     static_cast<int>(self.VideoModel.confirmKind()));
    self.Video->syncConfirmPage();
  }
}

void DebugCommands::videoWindowCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  int seconds = 0;
  if (gEngfuncs.Cmd_Argc() >= 2)
    seconds = atoi(gEngfuncs.Cmd_Argv(1));
  currentModule()->VideoModel.setConfirmWindowMsForTesting(
      seconds > 0 ? seconds * 1000 : 0);
  abc::ConsolePrint("[AbcUi] video confirm window: %ds\n",
                    seconds > 0 ? seconds : 15);
}

void DebugCommands::videoRevertCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  if (self.VideoModel.revert())
    abc::ConsolePrint("[AbcUi] video reverted\n");
  else
    abc::ConsoleWarn("[AbcUi] revert refused/failed\n");
  self.Video->syncConfirmPage();
}

} // namespace gameui
#endif
