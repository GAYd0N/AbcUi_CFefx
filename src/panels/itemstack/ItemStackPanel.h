#pragma once

// panels::ItemStackPanel - item pickup stack (hud.txt sprite), left of the
// crosshair area. Ported from ABCEnchance src/hud/vgui/itemstack.*
// (ItemStackPanel.res: c-95/c-128 anchor, 30x195, item 30x30, keep 5s).

#include "../../framework/HudPanel.h"
#include "../../framework/StackBase.h"

namespace panels {

class ItemStackPanel : public framework::HudPanel, public framework::StackBase {
public:
  ItemStackPanel();

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;
  void reset() override;
};

} // namespace panels
