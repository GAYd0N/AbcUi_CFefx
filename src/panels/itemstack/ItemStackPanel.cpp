// panels::ItemStackPanel implementation - see ItemStackPanel.h.
// from: ABCEnchance/src/hud/vgui/itemstack.* (GBK->UTF-8).

#include "ItemStackPanel.h"

#include <RmlUi/Core/Element.h>

#include <format>
#include <string>

#include "../../data/NetworkMessages.h"
#include "../../rmlui/texture/SpriteResource.h"

namespace panels {

ItemStackPanel::ItemStackPanel()
    : HudPanel("itemstack", "abc/abcui/hud/rml/itemstack.rml") {
  // Old ItemStackPanel.res.
  StackParams.keepTime = 5.0f;
  StackParams.fadeIn = 0.1f;
  StackParams.fadeOut = 0.3f;
  StackParams.maxItems = 195 / 30; // tall 195 / item_tall 30
}

void ItemStackPanel::onDocumentReady() {
  show();
  initStack(Document, "stack");

  subscribe(netmsg::ItemPickup, [this](const char *name) {
    // Old g_EventItemPickup handler: unknown hud.txt names are skipped.
    std::string atlas;
    wrect_t rect{};
    if (!sprites::findHudSprite(name, atlas, rect))
      return;
    const std::string item =
        std::format("<img class=\"icon\" src=\"spr://{}\"/>", name);
    appendItem("item", item.c_str());
  });
}

void ItemStackPanel::onData(double frameTime) {
  tickItems(frameTime);
}

void ItemStackPanel::reset() {
  clearItems();
}

} // namespace panels
