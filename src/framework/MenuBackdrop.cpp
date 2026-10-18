// framework::menubackdrop implementation - see MenuBackdrop.h.
#include "MenuBackdrop.h"

namespace framework::menubackdrop {

namespace {
Attacher Current = nullptr;
}

void setAttacher(Attacher attacher) {
  Current = attacher;
}

bool attach(Rml::ElementDocument *document) {
  if (!Current || !document)
    return false;
  Current(document);
  return true;
}

} // namespace framework::menubackdrop
