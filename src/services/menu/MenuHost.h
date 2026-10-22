#pragma once

// services::menu::Host - the replacement menu's presentation seam.
//
// The framework owns the native-UI takeover (PaintGate, the bridge) and the
// navigation state machine; the pages are modules. This service is the one
// place where both sides meet without either linking the other:
//
//   OUT  module -> framework  openPage/back/ensureUp/resume (the menu
//                             section's actions)
//   IN   framework -> module  the presentation snapshot, published as
//                             "menu.state" (transitions) and "menu.paint"
//                             (every presentation beat)
//
// The composition root (GameUiModule until the last page moves out) installs
// the callbacks; the accessor is process-lifetime (the AGENTS.md service
// rule), so a page module can publish/query without holding a module pointer.

#include <abcui/module_api.h>

#include <cstdint>
#include <functional>

namespace services {
namespace menu {

// One presentation snapshot; the fields mirror AbcUiMenuState.
struct Snapshot {
  int32_t phase = ABCUI_MENU_PHASE_BOOTING;
  int32_t page = ABCUI_MENU_PAGE_NONE;
  int32_t up = 0;
  int32_t loading = 0;
  int32_t consoleOpen = 0;
  int32_t hasLevel = 0;
  int32_t canClose = 0;
  uint32_t generation = 0;
  // ABCUI_MENU_OVERLAY_* bitmask of the overlays holding the screen.
  int32_t overlays = 0;
  // The live attempt's kind (ABCUI_MENU_ATTEMPT_*).
  int32_t attemptKind = ABCUI_MENU_ATTEMPT_NONE;
};

struct Host {
  std::function<Snapshot()> snapshot;
  std::function<int32_t(int32_t page)> openPage;
  std::function<void()> back;
  std::function<int32_t()> ensureUp;
  std::function<int32_t()> resume;
  std::function<void()> quit;
  // Resolve the quit confirmation overlay (1 quit / 0 cancel); the page
  // module's buttons call this through the sdk menu section.
  std::function<void(int32_t)> quitResolve;
  std::function<void()> disconnect;
  std::function<void()> toggleConsole;
  // Toggles the direct-connect overlay (the shell's entry when declared).
  std::function<void()> directConnect;
  // Confirmed exit without the dialog (the emergency page's Quit button: a
  // fault-time exit must not depend on a page that may be the broken one).
  std::function<void()> quitNow;
};

Host &host();

// Writes the current snapshot into an ABI struct (structSize is set; the
// caller still applies its own size cut).
void fill(AbcUiMenuState &out);

// Publishes the current snapshot: "menu.state" only when a field changed,
// "menu.paint" on every call. Re-entrant calls (a sink opening a page) are
// deferred until the running dispatch finishes, so no sink ever observes a
// half-published state or a nested dispatch.
void publish();

} // namespace menu
} // namespace services
