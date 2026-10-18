#pragma once

// framework::PanelManager - panel registry + lifecycle.
//
// The manager owns the panel list and routes the HUD pump, but it does not
// know the concrete panel types: modules/HudPanelCatalog.cpp fixes the
// construction order and calls installPanels() once per init cycle. This
// keeps framework free of panel includes (the old Viewport god object's
// "hold and route 25 panels" duty without the compile-time dependency).

#include <RmlUi/Core/Context.h>

#include <memory>
#include <vector>

namespace framework {

class HudPanel;

class PanelManager {
public:
  // Takes ownership of the ordered panel list. Called by HudPanelCatalog
  // before initAll(); a second call replaces the previous list.
  static void installPanels(std::vector<std::unique_ptr<HudPanel>> panels);
  // Loads every installed panel's document.
  static void initAll(Rml::Context *context);
  // HUD_VidInit: resolution changes + per-panel map reset.
  static void vidInitAll();
  // Level lifecycle gate. On disconnect reset/hide every HUD document,
  // releasing HUD-only owners without dismissing the independent menu.
  static void setSessionActive(bool active);
  // HUD_Frame: per-panel data refresh.
  static void onFrame(double frameTime);
  // HUD_Redraw: render-phase pump for projection consumers; runs a second
  // Context::Update so the same-frame Rml render picks the new positions up.
  static void onRedraw(double frameTime);
  static void shutdown();

  static const std::vector<std::unique_ptr<HudPanel>> &panels() {
    return Panels();
  }
  // Name lookup for the fixed seams that cannot capture a panel pointer
  // (engine command callbacks / input router free functions). Null before
  // install or after shutdown.
  static HudPanel *find(const char *name);

private:
  static bool SessionActive;
  static std::vector<std::unique_ptr<HudPanel>> &Panels();
};

} // namespace framework
