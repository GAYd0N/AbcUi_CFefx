// panels::AmmoStackPanel implementation - see AmmoStackPanel.h.
// from: ABCEnchance/src/hud/vgui/ammostack.* (GBK->UTF-8).

#include "AmmoStackPanel.h"

#include <RmlUi/Core/Element.h>

#include <cstdio>

#include "../../data/NetworkMessages.h"
#include "../../data/SpriteResource.h"
#include "../../data/WeaponResource.h"

namespace panels {

AmmoStackPanel::AmmoStackPanel()
	: HudPanel("ammostack", "abc/abcui/hud/rml/ammostack.rml") {
	// Old AmmoStackPanel.res.
	StackParams.keepTime = 5.0f;
	StackParams.fadeIn = 0.1f;
	StackParams.fadeOut = 0.3f;
	StackParams.maxItems = 200 / 20; // tall 200 / item_tall 20
}

void AmmoStackPanel::onDocumentReady() {
	// The container draws nothing while empty; keep the document shown.
	show();
	initStack(Document, "stack");

	subscribe(netmsg::AmmoPickup, [this](int index, int count) {
		// Old AddAmmoPickup: icon from any weapon carrying the ammo type.
		std::string atlas;
		wrect_t rect{};
		if (!weaponres::ammoIcon(index, atlas, rect))
			return;
		// Server-provided atlases live under subdirectories
		// ("bdsc_custom/..."), so the markup needs room for the full sprite
		// path plus the rect and count; 160 overflowed sprintf_s (fatal
		// invalid-parameter handler) on a long path.
		char item[256];
		sprintf_s(item,
			"<img class=\"icon\" src=\"spricon://%s/%d,%d,%d,%d\"/>"
			"<div class=\"count font-default\">%d</div>",
			atlas.c_str(), rect.left, rect.top, rect.right, rect.bottom, count);
		appendItem("item", item);
	});
}

void AmmoStackPanel::onData(double frameTime) {
	tickItems(frameTime);
}

void AmmoStackPanel::reset() {
	// Old CBaseStackPanel::Reset: drop all items (and re-show, the container
	// is transparent so an empty stack costs nothing).
	clearItems();
}

}
