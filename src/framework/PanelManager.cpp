// framework::PanelManager implementation - see PanelManager.h.
#include "PanelManager.h"

#include "HudPanel.h"

#include <abc/Console.h>

#include <utility>

namespace framework {

std::vector<std::unique_ptr<HudPanel>> &PanelManager::Panels() {
  static std::vector<std::unique_ptr<HudPanel>> panels;
  return panels;
}

void PanelManager::installPanels(
    std::vector<std::unique_ptr<HudPanel>> panels) {
  Panels() = std::move(panels);
}

void PanelManager::initAll(Rml::Context *context) {
  if (!context)
    return;
  if (Panels().empty()) {
    abc::ConsoleWarn("[AbcUi] no HUD panels installed; the HUD_Init panel "
                     "install was not called\n");
    return;
  }
  for (auto &panel : Panels()) {
    panel->loadDocument(context);
    panel->setSessionActive(SessionActive);
  }
  abc::ConsolePrint("[AbcUi] %d panel(s) loaded\n",
                    static_cast<int>(Panels().size()));
}

void PanelManager::vidInitAll() {
  // Old Viewport::VidInit ended with Reset(): every panel back to its
  // pre-map state on every map load / video init.
  for (auto &panel : Panels())
    panel->reset();
}

HudPanel *PanelManager::find(const char *name) {
  if (!name)
    return nullptr;
  for (const auto &panel : Panels()) {
    if (panel->getName() == name)
      return panel.get();
  }
  return nullptr;
}

void PanelManager::shutdown() {
  Panels().clear();
  SessionActive = false;
}

} // namespace framework
