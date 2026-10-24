// framework::ModulePanel implementation - see ModulePanel.h.
#include "ModulePanel.h"

#include <utility>

namespace framework {
ModulePanel::ModulePanel(const AbcUiPanelDesc &desc,
                         module_resources::OwnerHandle owner)
    : HudPanel(desc.name, desc.documentPath, desc.zIndex), Desc(desc),
      Owner(std::move(owner)) {}

void ModulePanel::onDocumentReady() {
  module_resources::invoke(Owner, "panel ready", Desc.onDocumentReady,
                           Desc.userdata);
}

void ModulePanel::onData(double frameTime) {
  module_resources::invoke(Owner, "panel data", Desc.onData, Desc.userdata,
                           frameTime);
}

void ModulePanel::onRedraw(double frameTime) {
  module_resources::invoke(Owner, "panel redraw", Desc.onRedraw, Desc.userdata,
                           frameTime);
}

void ModulePanel::reset() {
  module_resources::invoke(Owner, "panel reset", Desc.reset, Desc.userdata);
}

} // namespace framework
