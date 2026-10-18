// panels::AmmoStackPanel implementation - see AmmoStackPanel.h.
// from: ABCEnchance/src/hud/vgui/ammostack.* (GBK->UTF-8).

#include "AmmoStackPanel.h"

#include <RmlUi/Core/Element.h>

#include <format>
#include <string>

#include "../../data/NetworkMessages.h"
#include "../../rmlui/texture/SpriteResource.h"
#include "../../data/WeaponResource.h"
#include "../../framework/GlyphText.h"

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
    // ("bdsc_custom/..."). The markup is composed as std::string, so the
    // full sprite path cannot overflow a fixed buffer (the old 160-byte
    // buffer hit the sprintf_s invalid-parameter handler on long paths).
    // Count: ink-only additive glyphs (no black backing), the same readout
    // ink as the score and the flashlight percentage; a value outside the
    // glyph set falls back to plain type.
    const std::string countText = std::to_string(count);
    const std::string countInk =
        framework::glyphs::inkMarkup(countText, framework::glyphs::kAccentTint);
    const std::string item =
        std::format("<img class=\"icon\" src=\"spricon://{}/{},{},{},{}\"/>"
                    "<div class=\"count font-default\">{}</div>",
                    atlas, rect.left, rect.top, rect.right, rect.bottom,
                    countInk.empty() ? countText : countInk);
    appendItem("item", item.c_str());
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

} // namespace panels
