// services::menu::Host implementation - see MenuHost.h.
#include "MenuHost.h"

#include <abcui/events.h>

#include "../../modsys/EventBus.h"

namespace services {
namespace menu {
namespace {

bool sameState(const Snapshot &a, const Snapshot &b) {
  return a.phase == b.phase && a.page == b.page && a.up == b.up &&
         a.loading == b.loading && a.consoleOpen == b.consoleOpen &&
         a.hasLevel == b.hasLevel && a.canClose == b.canClose &&
         a.generation == b.generation && a.overlays == b.overlays &&
         a.attemptKind == b.attemptKind;
}

void toAbi(const Snapshot &snapshot, AbcUiMenuState &out) {
  out.structSize = sizeof(out);
  out.phase = snapshot.phase;
  out.page = snapshot.page;
  out.up = snapshot.up;
  out.loading = snapshot.loading;
  out.consoleOpen = snapshot.consoleOpen;
  out.hasLevel = snapshot.hasLevel;
  out.canClose = snapshot.canClose;
  out.generation = snapshot.generation;
  out.overlays = snapshot.overlays;
  out.attemptKind = snapshot.attemptKind;
}

} // namespace

Host &host() {
  static Host instance;
  return instance;
}

void fill(AbcUiMenuState &out) {
  out = AbcUiMenuState{};
  const Host &seam = host();
  if (!seam.snapshot) {
    out.structSize = static_cast<uint32_t>(sizeof(AbcUiMenuState));
    out.page = ABCUI_MENU_PAGE_NONE;
    return;
  }
  toAbi(seam.snapshot(), out);
}

void publish() {
  const Host &seam = host();
  if (!seam.snapshot)
    return;
  // A sink may call back into the menu section (openPage/back); that call
  // dispatches again and reaches publish() re-entrantly. Defer instead of
  // nesting: the outer loop re-reads the snapshot and publishes the settled
  // state exactly once more.
  static bool publishing = false;
  static bool pending = false;
  static bool hasLast = false;
  static Snapshot last;
  if (publishing) {
    pending = true;
    return;
  }
  publishing = true;
  do {
    pending = false;
    const Snapshot now = seam.snapshot();
    AbcUiMenuState payload{};
    toAbi(now, payload);
    if (!hasLast || !sameState(now, last)) {
      last = now;
      hasLast = true;
      framework::eventBus().publish(ABCUI_CHANNEL_MENU_STATE, payload);
    }
    framework::eventBus().publish(ABCUI_CHANNEL_MENU_PAINT, payload);
  } while (pending);
  publishing = false;
}

} // namespace menu
} // namespace services
