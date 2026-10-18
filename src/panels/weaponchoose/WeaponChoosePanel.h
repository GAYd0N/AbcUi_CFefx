#pragma once

// panels::WeaponChoose - the Ecco-style weapon selection bar (old
// CWeaponChoosePanel, from: ABCEnchance/src/hud/vgui/weaponchoose.*,
// GBK->UTF-8). Slot icons across the top; owned weapons under each slot;
// the selected slot expands and the bucket frame marks the highlight.
// Selection rides data::weaponres::selectSlot (slot keys / invnext /
// invprev through the command bus, TextMenu-open gated); +attack confirms
// the highlighted pick (old vgui mouse-press confirm - normal play has no
// vgui cursor, so the attack key is the honest equivalent; D-17 note).
//
// hud_fastswitch never shows the bar (the command already went out); the
// hud_rainbow_menu cycle rides the rebuild (old PerformLayout counter).

#include "../../framework/HudPanel.h"

namespace panels {

namespace weaponchoose {
// HUD_Init-time: subscribes slot/invnext/invprev/+attack on the command bus
// (the TextMenu-open gate lives here, next to the menu's own chain).
void registerCvars();
void registerCommands();
bool textmenuOpen();
// +attack seam: attempt the confirm while the bar shows (consumed = true).
bool attackConfirm();
} // namespace weaponchoose

class WeaponChoosePanel : public framework::HudPanel {
public:
  WeaponChoosePanel();
  ~WeaponChoosePanel() override;

  void reset() override;

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;

private:
  // Rebuilds the bar DOM from weaponres (old PerformLayout); keeps the
  // panel open on selection changes.
  void rebuild();
  bool shouldDraw() const;
  // Old SelectWeapon: confirm the highlight (ammo gate + select sound);
  // true = the command went out (old m_bSelectBlock armed only on success).
  bool confirmSelection();
  // +attack while the bar shows: attempt the confirm, swallow the press
  // only when it confirmed (old confirm + BlockAttackOnce pair).
  bool attackConfirm();
  void setVisible(bool visible);

  friend bool weaponchoose::attackConfirm();

  double CloseAtTime = 0.0;
  double ClientTimeNow = 0.0;
  bool Visible = false;
  int RainbowCounter = 0;
};

} // namespace panels
