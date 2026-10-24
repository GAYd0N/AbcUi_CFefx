#pragma once

// framework::ModulePanel - a HudPanel whose controller lives in a module.
//
// The module registers an AbcUiPanelDesc during onLoad; the framework owns
// the C++/Rml side (document lifetime, visibility, pumps) and forwards the
// pump virtuals through module ownership scopes. Host guards defensively
// handle exceptions that reach them; modules must not throw across the ABI.

#include <abcui/module_api.h>

#include "../framework/HudPanel.h"
#include "ModuleResources.h"

namespace framework {

class ModulePanel final : public ::framework::HudPanel {
public:
  ModulePanel(const AbcUiPanelDesc &desc, module_resources::OwnerHandle owner);

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;
  void onRedraw(double frameTime) override;
  void reset() override;

private:
  AbcUiPanelDesc Desc;
  module_resources::OwnerHandle Owner;
};

} // namespace framework
