// The microphone hardware card: the device list, the test toggle and the
// level meters projected from the voice facts.

#include "../shared/PageShared.h"
#include "SettingsInternal.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <string>

namespace settingspage {

// ------------------------------------------------ microphone hardware -----

void renderVoiceSectionImpl() {
  if (!Page)
    return;
  AbcUiElement *section = node("voice-section");
  if (!section)
    return;
  AbcUiVoiceFacts facts{};
  const bool available = voiceFacts(facts) && facts.available;
  const float level = available ? facts.speakingLevel : 0.0f;
  if (AbcUiElement *fill = node("voice-meter-fill")) {
    const float clamped = std::clamp(level, 0.0f, 1.0f);
    const std::string width = std::format("{:g}%", clamped * 100.0f);
    setProperty(fill, "width", width.c_str());
    setProperty(fill, "background-color",
                level > 0.85f  ? "#dd634f"
                : level > 0.6f ? "#d4bd4c"
                               : "#68bd66");
  }
  Api->ui->elementSetClass(section, "video-unavailable", !available);

  AbcUiElement *volumeInput = node("voice-mic-volume");
  AbcUiElement *boostToggle = node("voice-mic-boost");
  AbcUiElement *testToggle = node("voice-mic-test");
  AbcUiElement *status = node("voice-status");
  if (!available) {
    setInnerRml(status,
                escapeText(resolve("#abcui.gameui/Settings_VoiceUnavailable")));
    Api->ui->elementSetDisabled(volumeInput, 1);
    Api->ui->elementSetClass(boostToggle, "video-disabled", 1);
    Api->ui->elementSetClass(testToggle, "video-disabled", 1);
    return;
  }
  Api->ui->elementSetDisabled(volumeInput, 0);

  // The volume field shows the 0..100 display scale (the original page's
  // GetControlFloat*100 mapping). The origin field of a change commit is
  // skipped like every text row (caret/IME survival).
  if (volumeInput && volumeInput != VoiceVolumeSkip) {
    const float shown = facts.micDirty ? facts.micVolumeDraft : facts.micVolume;
    const std::string buffer =
        std::format("{}", static_cast<int>(shown * 100.0f + 0.5f));
    Api->ui->elementSetValue(volumeInput, buffer.c_str());
    setInnerRml(node("voice-mic-value"), buffer + "%");
  }
  if (boostToggle) {
    const bool on =
        facts.micBoostDirty ? facts.micBoostDraft != 0 : facts.micBoost != 0;
    setInnerRml(boostToggle,
                escapeText(resolve(on ? "#abcui.gameui/Settings_On"
                                      : "#abcui.gameui/Settings_Off")));
    Api->ui->elementSetClass(boostToggle, "video-disabled", 0);
  }
  if (testToggle) {
    setInnerRml(testToggle,
                escapeText(resolve(
                    facts.testing ? "#abcui.gameui/Settings_VoiceStopTest"
                                  : "#abcui.gameui/Settings_VoiceStartTest")));
    Api->ui->elementSetClass(testToggle, "video-disabled", 0);
  }

  if (status) {
    std::string text;
    if (facts.testing) {
      // The level is the engine's GetSpeakingVolume answer, rendered as a
      // plain number - never an animated bar pretending to be a sample.
      const int percent = static_cast<int>(facts.speakingLevel * 100.0f + 0.5f);
      text = resolve("#abcui.gameui/Settings_VoiceLevel") + " " +
             std::to_string(percent) + "%";
    } else {
      text = resolve("#abcui.gameui/Settings_VoiceIdle");
    }
    if (facts.micDirty || facts.micBoostDirty)
      text += " | " + resolve("#abcui.gameui/Settings_Dirty");
    if (facts.micBoostDirty) {
      text += " | " + resolve("#abcui.gameui/Settings_MicBoost") + " " +
              resolve(facts.micBoostDraft ? "#abcui.gameui/Settings_On"
                                          : "#abcui.gameui/Settings_Off");
    }
    setInnerRml(status, escapeText(text));
  }
}

void renderVoiceSection() {
  renderVoiceSectionImpl();
}

} // namespace settingspage
