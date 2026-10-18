// Session lifetime is distinct from engine/plugin lifetime: disconnect does
// not necessarily send HUD_Shutdown or HUD_VidInit, and the Rml context stays
// alive for the main menu.
#include "PanelManager.h"
#include "HudPanel.h"

namespace framework {

bool PanelManager::SessionActive = false;

void PanelManager::setSessionActive(bool active) {
  if (SessionActive == active)
    return;
  SessionActive = active;
  // Gate every document first, before reset callbacks can request shows.
  for (auto &panel : Panels())
    panel->setSessionActive(active);
  if (!active) {
    for (auto &panel : Panels()) {
      panel->reset(); // includes scoreboard's pointer token release
      panel->hide();  // not all panel reset implementations hide
    }
  }
}

void PanelManager::onFrame(double frameTime) {
  if (!SessionActive)
    return;
  for (auto &panel : Panels())
    panel->onData(frameTime);
}

void PanelManager::onRedraw(double frameTime) {
  if (!SessionActive)
    return;
  for (auto &panel : Panels())
    panel->onRedraw(frameTime);
}

} // namespace framework
