#pragma once

// panels::DeadMsgPanel - killfeed rows. Ported from ABCEnchance
// src/hud/vgui/deadmsg.*: rows fed by data::textfeed::KillFeed, capped by
// hud_deathnotice_count, rows fading out after hud_deathnotice_time
// (0.5s fade); hud_deathnotice toggles the whole feed (persisted contract).

#include "../../framework/HudPanel.h"

namespace panels {

namespace deadmsg {
// HUD_Init-time cvar registration (plan 08 §4.9).
void registerCvars();
} // namespace deadmsg

class DeadMsgPanel : public framework::HudPanel {
public:
  DeadMsgPanel();

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;
  void reset() override;

private:
  void addItem(const char *victim, const char *attacker, const char *inflictor);

  struct Row {
    Rml::Element *element = nullptr;
    double createTime = 0.0;
    double fadeStartTime = -1.0;
  };
  double ClientTimeNow = 0.0;
  std::vector<Row> Rows;
};

} // namespace panels
