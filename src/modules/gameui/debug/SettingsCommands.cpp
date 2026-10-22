// Debug-only settings commands; extracted without changing command names.
#ifndef NDEBUG
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include "modules/gameui/GameUiModule.h"
#include "DebugCommands.h"
#include <abc/Console.h>
#include "modsys/ApiTable.h"
#include "rmlui/InputBridge.h"
#include <string>
namespace gameui {
void DebugCommands::voiceStatusCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  abc::ConsolePrint(
      "[AbcUi] voice available=%d testing=%d mic=%.3f micDraft=%.3f "
      "micDirty=%d boost=%d boostDraft=%d boostDirty=%d level=%.3f\n",
      self.VoiceModel.available() ? 1 : 0, self.VoiceModel.testing() ? 1 : 0,
      self.VoiceModel.micVolume(), self.VoiceModel.micVolumeDraft(),
      self.VoiceModel.micDirty() ? 1 : 0, self.VoiceModel.micBoost() ? 1 : 0,
      self.VoiceModel.micBoostDraft() ? 1 : 0,
      self.VoiceModel.micBoostDirty() ? 1 : 0, self.VoiceModel.speakingLevel());
}

void DebugCommands::voiceBoostCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  if (!self.VoiceModel.available()) {
    abc::ConsoleWarn("[AbcUi] voice unavailable\n");
    return;
  }
  const bool current = self.VoiceModel.micBoostDirty()
                           ? self.VoiceModel.micBoostDraft()
                           : self.VoiceModel.micBoost();
  self.VoiceModel.setMicBoostDraft(!current);
  framework::SettingsApi::bumpRevision();
  voiceStatusCommand();
}

void DebugCommands::voiceTestCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  if (self.VoiceModel.testing()) {
    self.VoiceModel.stopTest();
    abc::ConsolePrint("[AbcUi] voice test STOPPED\n");
  } else if (self.VoiceModel.startTest()) {
    abc::ConsolePrint(
        "[AbcUi] voice test STARTED (engine accepted the tweak mode)\n");
  } else {
    abc::ConsoleWarn(
        "[AbcUi] voice test REFUSED (no device / engine returned 0)\n");
  }
  framework::SettingsApi::bumpRevision();
  voiceStatusCommand();
}

void DebugCommands::voiceApplyCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  if (!self.VoiceModel.available()) {
    abc::ConsoleWarn("[AbcUi] voice unavailable\n");
    return;
  }
  const bool micOk = self.VoiceModel.applyMicVolume();
  const bool boostOk = self.VoiceModel.applyMicBoost();
  abc::ConsolePrint("[AbcUi] voice apply mic=%d boost=%d\n", micOk ? 1 : 0,
                    boostOk ? 1 : 0);
  framework::SettingsApi::bumpRevision();
  voiceStatusCommand();
}

void DebugCommands::settingsApplyCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  const auto results = self.SettingsModel.apply();
  abc::ConsolePrint("[AbcUi] settings apply rows=%d\n",
                    static_cast<int>(results.size()));
  for (const auto &result : results)
    abc::ConsolePrint("[AbcUi] apply '%s' outcome=%d\n", result.cvar.c_str(),
                      static_cast<int>(result.outcome));
  framework::SettingsApi::bumpRevision();
  settingsStatusCommand();
}

void DebugCommands::settingsStatusCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  abc::ConsolePrint(
      "[AbcUi] settings rows=%d voice=%d testing=%d mic=%.2f boost=%d\n",
      static_cast<int>(self.SettingsModel.rows().size()),
      self.VoiceModel.available() ? 1 : 0, self.VoiceModel.testing() ? 1 : 0,
      self.VoiceModel.micVolume(), self.VoiceModel.micBoost() ? 1 : 0);
  for (const auto &view : self.SettingsModel.rows()) {
    if (view.state == services::settings::RowState::kMissing)
      continue; // registered rows only, keeps the dump readable
    abc::ConsolePrint(
        "[AbcUi] row '%s' kind=%d state=%d draft='%s' engine='%s'\n",
        view.row->Cvar, static_cast<int>(view.row->Kind),
        static_cast<int>(view.state), view.draftText.c_str(),
        view.engineText.empty() ? "" : view.engineText.c_str());
  }
}

void DebugCommands::bindStatusCommand() {
  if (!currentModule()) {
    abc::ConsoleWarn("[AbcUi] no module instance\n");
    return;
  }
  GameUiModule &self = *currentModule();
  unsigned wheelPulses = 0, wheelDelivered = 0;
  rmlui::wheelDiagCounts(wheelPulses, wheelDelivered);
  float panelScroll = -1.0f, panelScrollHeight = -1.0f;
  std::string hoverId = "(none)";
  if (self.Context) {
    if (Rml::Element *hover = self.Context->GetHoverElement())
      hoverId = hover->GetId().empty() ? hover->GetTagName() : hover->GetId();
    // The settings page is a module document now: its scroll state is not
    // reachable from the console (the page's own DOM owns it).
  }
  abc::ConsolePrint(
      "[AbcUi] binds: reads=%d capturing=%d row=%d conflict=%d token=%d "
      "clamp=%d\n",
      self.BindModel.readsAvailable() ? 1 : 0,
      self.BindModel.capturing() ? 1 : 0, self.BindModel.captureRow(),
      self.BindModel.conflictPending() ? 1 : 0,
      self.CaptureToken.held() ? 1 : 0, rmlui::keyCaptureClampActive() ? 1 : 0);
  abc::ConsolePrint("[AbcUi] wheel: pulses=%u delivered=%u panelScroll=%.1f "
                    "scrollHeight=%.1f hover=%s\n",
                    wheelPulses, wheelDelivered, panelScroll, panelScrollHeight,
                    hoverId.c_str());
  for (const auto &view : self.BindModel.rows()) {
    if (view.header)
      continue;
    if (view.state == services::settings::BindRowState::kClean &&
        view.draftKeyNames.empty())
      continue; // only active rows, keeps the dump readable
    std::string keys;
    for (const auto &key : view.draftKeyNames)
      keys += key + " ";
    abc::ConsolePrint("[AbcUi] bind %d '%s' state=%d keys=[%s]\n",
                      view.actionIndex,
                      view.action ? view.action->Command.c_str() : "?",
                      static_cast<int>(view.state), keys.c_str());
  }
}

} // namespace gameui
#endif
