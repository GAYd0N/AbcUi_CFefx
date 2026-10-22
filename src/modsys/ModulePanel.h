#pragma once

// framework::ModulePanel - a HudPanel whose controller lives in a module.
//
// The module registers an AbcUiPanelDesc during onLoad; the framework owns
// the C++/Rml side (document lifetime, visibility, pumps) and forwards the
// pump virtuals to the descriptor callbacks. Callbacks are wrapped so an
// escaping module exception is reported and contained instead of unwinding
// through the engine frame.

#include <abcui/module_api.h>

#include "../framework/HudPanel.h"

namespace framework {

class ModulePanel final : public ::framework::HudPanel {
public:
  explicit ModulePanel(const AbcUiPanelDesc &desc);

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;
  void onRedraw(double frameTime) override;
  void reset() override;

private:
  AbcUiPanelDesc Desc;
};

} // namespace framework
