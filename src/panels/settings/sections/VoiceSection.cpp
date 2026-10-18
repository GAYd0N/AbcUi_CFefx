// Microphone controls only; extracted from SettingsPage.cpp (UTF-8).
#include "panels/settings/SettingsUi.h"
#include "VoiceSection.h"
#include "services/settings/VoiceModel.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include "framework/TextUtil.h"
#include <abc/Format.h>
#include <cstdio>
#include <algorithm>
#include <string>
namespace panels::settings::sections {
using namespace detail;
void renderVoice(Rml::ElementDocument *doc,
                 services::settings::VoiceModel *Model,
                 Rml::Element *SkipField) {
  if (!doc)
    return;
  Rml::Element *section = doc->GetElementById("voice-section");
  if (!section)
    return;
  if (auto *Fill = doc->GetElementById("voice-meter-fill")) {
    const float Level =
        Model && Model->available() ? Model->speakingLevel() : 0;
    Fill->SetProperty(
        "width", std::to_string(std::clamp(Level, 0.0f, 1.0f) * 100) + "%");
    Fill->SetProperty("background-color", Level > 0.85f  ? "#dd634f"
                                          : Level > 0.6f ? "#d4bd4c"
                                                         : "#68bd66");
  }
  section->SetClass("video-unavailable", !Model || !Model->available());

  Rml::Element *volumeInput = doc->GetElementById("voice-mic-volume");
  Rml::Element *boostToggle = doc->GetElementById("voice-mic-boost");
  Rml::Element *testToggle = doc->GetElementById("voice-mic-test");
  Rml::Element *status = doc->GetElementById("voice-status");
  if (!Model || !Model->available()) {
    if (status)
      status->SetInnerRML(framework::TextUtil::escapeRml(
          translated("#Settings_VoiceUnavailable")));
    if (volumeInput)
      if (Rml::ElementFormControl *form =
              rmlui_dynamic_cast<Rml::ElementFormControl *>(volumeInput))
        form->SetDisabled(true);
    if (boostToggle)
      boostToggle->SetClass("video-disabled", true);
    if (testToggle)
      testToggle->SetClass("video-disabled", true);
    return;
  }
  if (volumeInput)
    if (Rml::ElementFormControl *form =
            rmlui_dynamic_cast<Rml::ElementFormControl *>(volumeInput))
      form->SetDisabled(false);

  // The volume field shows the 0..100 display scale (the original page's
  // GetControlFloat*100 mapping). The origin field of a change commit is
  // skipped like every text row (caret/IME survival).
  if (volumeInput && volumeInput != SkipField) {
    const float shown =
        Model->micDirty() ? Model->micVolumeDraft() : Model->micVolume();
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d",
                  static_cast<int>(shown * 100.0f + 0.5f));
    if (Rml::ElementFormControl *form =
            rmlui_dynamic_cast<Rml::ElementFormControl *>(volumeInput))
      form->SetValue(buffer);
    if (auto *Value = doc->GetElementById("voice-mic-value"))
      Value->SetInnerRML(std::string(buffer) + "%");
  }
  if (boostToggle) {
    const bool on =
        Model->micBoostDirty() ? Model->micBoostDraft() : Model->micBoost();
    boostToggle->SetInnerRML(framework::TextUtil::escapeRml(
        translated(on ? "#Settings_On" : "#Settings_Off")));
    boostToggle->SetClass("video-disabled", false);
  }
  if (testToggle) {
    testToggle->SetInnerRML(framework::TextUtil::escapeRml(
        translated(Model->testing() ? "#Settings_VoiceStopTest"
                                    : "#Settings_VoiceStartTest")));
    testToggle->SetClass("video-disabled", false);
  }

  if (status) {
    std::string text;
    if (Model->testing()) {
      // The level is the engine's GetSpeakingVolume answer, rendered
      // as a plain number - never an animated bar pretending to be a
      // sample (T13b boundary).
      const int percent =
          static_cast<int>(Model->speakingLevel() * 100.0f + 0.5f);
      text = abc::Format("{0} {1}%", translated("#Settings_VoiceLevel"),
                         std::to_string(percent));
    } else {
      text = translated("#Settings_VoiceIdle");
    }
    if (Model->isDirty())
      text += " | " + translated("#Settings_Dirty");
    if (Model->micBoostDirty())
      text += " | " +
              abc::Format("{0} {1}", translated("#Settings_MicBoost"),
                          translated(Model->micBoostDraft() ? "#Settings_On"
                                                            : "#Settings_Off"));
    status->SetInnerRML(framework::TextUtil::escapeRml(text));
  }
}
} // namespace panels::settings::sections
