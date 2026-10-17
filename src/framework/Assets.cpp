// framework::Assets implementation - see Assets.h.
#include "Assets.h"
#include "ResourceFile.h"

#include <abc/Console.h>

namespace framework {

namespace Assets {

namespace {
bool fileExists(const char* relativePath) {
	return ResourceFile::exists(relativePath);
}
} // namespace

void verifyInstalled() {
	struct Check {
		const char* Path;
		const char* What;
	};
	constexpr Check checks[] = {
		{"abc/abcui/rcss/base.rcss", "base stylesheet"},
		{"abc/abcui/rcss/theme.rcss", "shared menu theme"},
		{"abc/abcui/lang/english.txt", "localization"},
		{"abc/abcui/gameui/config/mainmenu.json", "menu definition"},
		{"abc/abcui/gameui/config/commandmenu.json", "command menu definition"},
		{"abc/abcui/gameui/scence/video/trainstation.mp4", "menu backdrop video"},
		{"abc/abcui/gameui/scence/audio/opfor.mp3", "menu music"},
		{"abc/abcui/hud/rml/health.rml", "health document"},
		{"abc/abcui/hud/rml/ammobar.rml", "ammo document"},
		{"abc/abcui/hud/rml/textmenu.rml", "textmenu document"},
		{"abc/abcui/hud/img/healthpanel_background.png", "health panel art"},
		{"abc/abcui/hud/img/ammobar_background.png", "ammo panel art"},
		{"abc/abcui/hud/img/flashlight_off.png", "flashlight art"},
		{"abc/abcui/hud/spr/weapon_error.txt", "weapon sprite fallback"},
		// Batch B (feedback panels).
		{"abc/abcui/hud/rml/crosshair.rml", "crosshair document"},
		{"abc/abcui/hud/rml/indicator.rml", "indicator document"},
		{"abc/abcui/hud/rml/gindicator.rml", "grenade indicator document"},
		{"abc/abcui/hud/rml/dmgtiles.rml", "damage tiles document"},
		{"abc/abcui/hud/rml/effect.rml", "effect document"},
		{"abc/abcui/hud/rml/popnum.rml", "pop number document"},
		{"abc/abcui/hud/rml/cfefx.rml", "cfefx document"},
		{"abc/abcui/hud/img/cfefx/NanoDamege1.png", "cfefx score art"},
		{"abc/abcui/hud/img/cfefx/NoBossStar.png", "cfefx star art"},
		{"abc/abcui/hud/img/cfefx/NoBossSide500.png", "cfefx damage mark art"},
		{"abc/abcui/hud/img/cfefx/NoBossEffect.png", "cfefx score effect art"},
		{"sound/abc/cfefx/UI_SPECIALKILL2.wav", "cfefx score sound"},
		{"abc/abcui/hud/img/efx_darkround.png", "danger vignette art"},
		{"abc/abcui/hud/img/pain_indicator.png", "pain indicator art"},
		{"abc/abcui/hud/img/grenadeicon.png", "grenade indicator icon"},
		{"abc/abcui/hud/img/icon_poison.png", "damage tile icon"},
		{"abc/abcui/hud/img/icon_shock.png", "damage tile icon"},
		{"abc/abcui/hud/spr/face_rainleft.spr", "water splash sprite"},
		{"abc/abcui/hud/spr/face_rainright.spr", "water splash sprite"},
		// Batch D (render depth panels).
		{"abc/abcui/hud/rml/itemhighlight.rml", "item highlight document"},
		{"abc/abcui/hud/ItemHighLightList.txt", "item highlight list"},
		{"abc/abcui/hud/img/itemhighlight/marker.png", "item highlight marker art"},
		{"abc/abcui/hud/rml/radar.rml", "radar document"},
		{"abc/abcui/hud/img/radar/radar_background.png", "radar plate art"},
		{"abc/abcui/hud/img/radar/radar_mask.png", "radar square mask"},
		// T05 interactive documents.
		{"abc/abcui/gameui/rml/mainmenu.rml", "main menu document"},
		{"abc/abcui/gameui/rcss/mainmenu.rcss", "main menu stylesheet"},
		// T10 settings page.
		{"abc/abcui/gameui/rml/settings.rml", "settings document"},
		{"abc/abcui/gameui/rcss/settings.rcss", "settings stylesheet"},
		// T12 video confirmation dialog.
		{"abc/abcui/gameui/rml/videoconfirm.rml", "video confirm document"},
		{"abc/abcui/gameui/rcss/videoconfirm.rcss", "video confirm stylesheet"},
		// In-game Rml command menu (+commandmenu replacement).
		{"abc/abcui/gameui/rml/commandmenu.rml", "command menu document"},
		{"abc/abcui/gameui/rcss/commandmenu.rcss", "command menu stylesheet"},
	};
	for (const auto& check : checks) {
		if (!fileExists(check.Path))
			abc::ConsoleWarn("[AbcUi] missing %s: %s\n", check.What, check.Path);
	}
}

}

}
