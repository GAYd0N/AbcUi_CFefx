#pragma once

// gameui::SettingsFlow - settings page presentation.
//
// Owns the "projected for this opening" latch. The page and the four models
// are owned by GameUiModule and injected here; present() takes the fresh
// snapshots once per opening, then just shows the page.

namespace panels::settings {
class SettingsPage;
}
namespace services::settings {
class SettingsModel;
class KeyBindModel;
class VideoModeModel;
class VoiceModel;
} // namespace services::settings

namespace gameui {

class SettingsFlow {
public:
  SettingsFlow(services::settings::SettingsModel &settings,
               services::settings::KeyBindModel &bind,
               services::settings::VideoModeModel &video,
               services::settings::VoiceModel &voice);

  void setPage(panels::settings::SettingsPage *page);
  void shutdown();
  bool presented() const {
    return Presented;
  }
  void setPresented(bool presented) {
    Presented = presented;
  }
  void hide();

  void present();

private:
  services::settings::SettingsModel &Settings;
  services::settings::KeyBindModel &Bind;
  services::settings::VideoModeModel &Video;
  services::settings::VoiceModel &Voice;
  panels::settings::SettingsPage *Page = nullptr;
  bool Presented = false;
};

} // namespace gameui
