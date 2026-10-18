// gameui::SettingsFlow implementation - see SettingsFlow.h.
#include "SettingsFlow.h"

#include "../../../panels/settings/SettingsPage.h"
#include "../../../services/settings/KeyBindModel.h"
#include "../../../services/settings/SettingsModel.h"
#include "../../../services/settings/VideoModeModel.h"
#include "../../../services/settings/VoiceModel.h"

namespace gameui {

SettingsFlow::SettingsFlow(services::settings::SettingsModel &settings,
                           services::settings::KeyBindModel &bind,
                           services::settings::VideoModeModel &video,
                           services::settings::VoiceModel &voice)
    : Settings(settings), Bind(bind), Video(video), Voice(voice) {}

void SettingsFlow::setPage(panels::settings::SettingsPage *page) {
  Page = page;
}

void SettingsFlow::shutdown() {
  Page = nullptr;
  Presented = false;
}

void SettingsFlow::hide() {
  if (Page)
    Page->hide();
  Presented = false;
}

void SettingsFlow::present() {
  if (!Page)
    return;
  if (!Presented) {
    Settings.snapshot();
    Bind.snapshot();
    Video.snapshot(); // fresh actual mode + enumerated list
    Voice.snapshot(); // fresh device floats + interface presence
    Page->render();
    Presented = true;
  }
  Page->show();
}

} // namespace gameui
