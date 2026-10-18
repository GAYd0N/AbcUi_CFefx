#pragma once

// panels::TextMenuPanel - vanilla ShowMenu replacement (number-key menus).
// Ported from ABCEnchance src/hud/vgui/textmenu.*: chunked ShowMenu
// accumulation (bits&0x7f = more-chunks pending, bit7 = ammo-selection
// menu), shutoff countdown, and the slot1..10 key interception - when the
// menu is open the chained slot commands turn into menuselect (old
// g_EventCmdSlot handler, consumed = the engine weapon-slot path runs only
// while the menu is closed).

#include "../../framework/HudPanel.h"

namespace panels {

namespace textmenu {
// The chained slot command for a slot index (0-based) - the framework bus
// instance (framework/commands.h), null before HUD_Init.
abc::CChainedCommand *command(int index);
// Logical menu-open state (old CTextMenu::IsVisible) - the WeaponChoose
// slot feed and the bus consumers gate on this.
bool isOpen();
} // namespace textmenu

class TextMenuPanel : public framework::HudPanel {
public:
  TextMenuPanel();
  ~TextMenuPanel() override;

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;
  void reset() override;

private:
  // Old SelectMenuItem: valid slot -> menuselect/as_menuselect + fade out.
  void selectMenuItem(int slot);
  void showMenu(int validSlots, int displaySeconds, int bits,
                const std::string &text);

  int ValidSlots = 0;
  bool IsASMenu = false;
  bool WaitingForMore = false;
  std::string MenuString;
  double ShutoffTime = -1.0;
  // Client time the fade-out ends and the document can hide (-1 = none
  // pending; old TextMenuOut SetVisible 0 at +0.2s).
  double HideAtTime = -1.0;
  double ClientTimeNow = 0.0;
};

} // namespace panels
