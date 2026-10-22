// framework::Assets implementation - see Assets.h.
#include "Assets.h"
#include "ResourceFile.h"

#include <abc/Console.h>

namespace framework {

namespace Assets {

namespace {
bool fileExists(const char *relativePath) {
  return ResourceFile::exists(relativePath);
}
} // namespace

void verifyInstalled() {
  struct Check {
    const char *Path;
    const char *What;
  };
  constexpr Check checks[] = {
      {"abc/abcui/rcss/base.rcss", "base stylesheet"},
      {"abc/abcui/rcss/theme.rcss", "shared menu theme"},
      {"abc/abcui/lang/english.txt", "localization"},
      {"abc/abcui/gameui/config/mainmenu.json", "menu definition"},
      {"abc/abcui/gameui/config/commandmenu.json", "command menu definition"},
      {"abc/abcui/gameui/scence/video/trainstation.mp4", "menu backdrop video"},
      {"abc/abcui/gameui/scence/audio/opfor.mp3", "menu music"},
      {"abc/abcui/hud/img/healthpanel_background.png", "health panel art"},
      {"abc/abcui/hud/img/ammobar_background.png", "ammo panel art"},
      {"abc/abcui/hud/spr/weapon_error.txt", "weapon sprite fallback"},
      // Batch B (feedback panels). Everything that moved into a module
      // package is validated by the loader instead; only the engine-played
      // cfefx score sound still ships with the framework.
      {"sound/abc/cfefx/UI_SPECIALKILL2.wav", "cfefx score sound"},
      // Batch D (render depth panels).
      // T05 interactive documents. The main menu document moved into the
      // abcui.gameui package; its stylesheet stays until the last menu page
      // moves (the built-in documents share it).
      {"abc/abcui/gameui/rcss/mainmenu.rcss", "main menu stylesheet"},
      // T10 settings page.
      {"abc/abcui/gameui/rml/settings.rml", "settings document"},
      {"abc/abcui/gameui/rcss/settings.rcss", "settings stylesheet"},
      // T12 video confirmation dialog.
      {"abc/abcui/gameui/rml/videoconfirm.rml", "video confirm document"},
      {"abc/abcui/gameui/rcss/videoconfirm.rcss", "video confirm stylesheet"},
      // The command menu (+commandmenu replacement) moved into the
      // abcui.gameui package with its page.
  };
  for (const auto &check : checks) {
    if (!fileExists(check.Path))
      abc::ConsoleWarn("[AbcUi] missing %s: %s\n", check.What, check.Path);
  }
}

} // namespace Assets

} // namespace framework
