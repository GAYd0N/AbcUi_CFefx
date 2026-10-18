#pragma once

// services::menumusic::MenuMusic - the menu's background music.
//
// Playback is the ENGINE's own mp3 player, driven exactly the way the legacy
// ABCEnchance plugin drove it: `mp3 loop <track> ui` starts the loop and
// `mp3 stop` ends it. The engine owns looping and loudness (the audio page's
// mp3volume row is the engine's music volume), so this service only tracks
// the on/off state: the track starts while a replacement page is on screen
// and stops the moment it goes away - never bleeding into gameplay.
//
// Deliberately narrow: it knows one mod-relative track path and the "menu is
// up" fact (the owner supplies it once per frame).

#include <string>

namespace menumusic {

class MenuMusic {
public:
  // Mod-relative track path as the engine's mp3 player spells it
  // ("abc/abcui/gameui/scence/audio/opfor.mp3" - the shape the legacy plugin's
  // "media/gamestartup.mp3" used). Empty disables playback entirely; a value
  // that is not a safe relative path is refused (the cvar is user-editable
  // and its text becomes part of an engine command line).
  void setSource(const std::string &trackPath);
  const std::string &source() const;
  void setEnabled(bool enabled);
  bool enabled() const;

  // Per-frame: starts/stops the track to match `menuVisible`.
  void update(bool menuVisible);

  // Stops playback now (shutdown / level start).
  void stop();

  // Diagnostics: whether the service has the track running (the engine owns
  // the actual playback and its loop).
  bool playing() const;

private:
  bool startTrack();

  std::string Source;
  bool On = true;
  bool Playing = false;
  // A refused (unsafe) source warns once, not every frame.
  bool Refused = false;
};

MenuMusic &menuMusic();

} // namespace menumusic
