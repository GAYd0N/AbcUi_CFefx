// services::menumusic::MenuMusic implementation - see MenuMusic.h.
#include "MenuMusic.h"

#include <metahook.h>

#include <abc/Console.h>
#include <abc/CvarCmd.h>

#include "../../framework/AssetPaths.h"
#include "services/settings/SettingsModel.h"

#include <string>

namespace menumusic {

void MenuMusic::setSource(const std::string &trackPath) {
  const std::string resolved =
      framework::assets::remapLegacyMenuMedia(trackPath);
  if (Source == resolved)
    return;
  stop();
  Source = resolved;
  Refused = false;
}

const std::string &MenuMusic::source() const {
  return Source;
}

void MenuMusic::setEnabled(bool enabled) {
  On = enabled;
  if (!On)
    stop();
}

bool MenuMusic::enabled() const {
  return On;
}

bool MenuMusic::playing() const {
  return Playing;
}

void MenuMusic::stop() {
  if (Playing)
    abc::EngineClientCmd("mp3 stop\n");
  Playing = false;
}

bool MenuMusic::startTrack() {
  if (Source.empty())
    return false;
  // The track text becomes part of an engine command line, so it must be a
  // safe relative path (the same rule the settings file rows are written
  // under) - a hand-typed cvar value could otherwise splice a command.
  if (!services::settings::fileChoiceValueValid(Source.c_str())) {
    if (!Refused) {
      Refused = true;
      abc::ConsoleWarn("[AbcUi music] refused track path: %s\n",
                       Source.c_str());
    }
    return false;
  }
  // Exactly the legacy plugin's call: the engine's mp3 player loops the
  // file and applies its own music volume (mp3volume).
  const std::string command = "mp3 loop " + Source + " ui\n";
  abc::EngineClientCmd(command.c_str());
  Playing = true;
  return true;
}

void MenuMusic::update(bool menuVisible) {
  if (!On || Source.empty() || !menuVisible) {
    if (Playing)
      stop();
    return;
  }
  if (!Playing)
    startTrack();
}

MenuMusic &menuMusic() {
  static MenuMusic Instance;
  return Instance;
}

} // namespace menumusic
