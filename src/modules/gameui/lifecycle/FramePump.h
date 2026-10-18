#pragma once

// The frame ordering contract shared by the module and offline integration
// tests: services are never gated by DOM readiness, visibility or focus.
// Callbacks keep engine/Rml headers and allocation out of this scheduler.
namespace gameui::lifecycle {

template <class Services, class Visible, class Presentation>
void pumpMenuFrame(Services &&PumpServices, Visible &&IsVisible,
                   Presentation &&Present) {
  PumpServices();
  if (IsVisible())
    Present();
}

} // namespace gameui::lifecycle
