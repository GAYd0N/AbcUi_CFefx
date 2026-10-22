// framework::ModulePanel implementation - see ModulePanel.h.
#include "ModulePanel.h"

#include <abc/Console.h>

namespace framework {
namespace {

template <typename Fn>
void guarded(const char *panel, const char *what, Fn &&fn) {
  try {
    fn();
  } catch (...) {
    abc::ConsoleError("[AbcUi] module panel \"%s\": exception escaped %s\n",
                      panel, what);
  }
}

} // namespace

ModulePanel::ModulePanel(const AbcUiPanelDesc &desc)
    : HudPanel(desc.name, desc.documentPath, desc.zIndex), Desc(desc) {}

void ModulePanel::onDocumentReady() {
  if (Desc.onDocumentReady)
    guarded(Name.c_str(), "onDocumentReady",
            [&] { Desc.onDocumentReady(Desc.userdata); });
}

void ModulePanel::onData(double frameTime) {
  if (Desc.onData)
    guarded(Name.c_str(), "onData",
            [&] { Desc.onData(Desc.userdata, frameTime); });
}

void ModulePanel::onRedraw(double frameTime) {
  if (Desc.onRedraw)
    guarded(Name.c_str(), "onRedraw",
            [&] { Desc.onRedraw(Desc.userdata, frameTime); });
}

void ModulePanel::reset() {
  if (Desc.reset)
    guarded(Name.c_str(), "reset", [&] { Desc.reset(Desc.userdata); });
}

} // namespace framework
