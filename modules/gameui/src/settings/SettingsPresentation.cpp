// Presentation: menu.state/menu.paint drive the document show/hide and the
// per-frame model beats; the routed ESC asks the page before the menu pops.

#include "../shared/PageShared.h"
#include "SettingsInternal.h"

namespace settingspage {

void present(bool focusDefault) {
  if (!Page || !Api->settings)
    return;
  Api->documents->show(Page);
  ensureSkeleton();
  if (!Presented) {
    // Fresh opening: the four models re-read their engine truth once, the
    // page then projects the settled state (the rest of the page's beats are
    // change-gated).
    Api->settings->snapshot();
    Presented = true;
    render();
  }
  if (focusDefault)
    refocusDefault();
}

void dismiss() {
  if (Page && Api->documents->visible(Page))
    Api->documents->hide(Page);
  Presented = false;
}

void onMenuState(void *, const void *payload) {
  const auto *state = static_cast<const AbcUiMenuState *>(payload);
  const bool wanted =
      state && state->up && state->page == ABCUI_MENU_PAGE_SETTINGS;
  if (!wanted) {
    dismiss();
    return;
  }
  AbcUiVideoFacts facts{};
  // The display confirmation outranks every other surface: the page stays
  // hidden until it resolves.
  if (videoFacts(facts) && facts.awaitingConfirm)
    return;
  present(state->overlays == 0);
}

void onMenuPaint(void *, const void *payload) {
  const auto *state = static_cast<const AbcUiMenuState *>(payload);
  const bool awaitingConfirm = projectVideoConfirm();
  const bool wanted =
      state && state->up && state->page == ABCUI_MENU_PAGE_SETTINGS;
  if (!wanted) {
    dismiss();
    return;
  }
  if (awaitingConfirm) {
    dismiss();
    return;
  }
  if (!Page)
    return;
  if (!Api->documents->visible(Page)) {
    present(state->overlays == 0);
    return;
  }
  if (!Api->settings)
    return;
  ensureSkeleton();
  // Model beats that need a per-frame check (cheap while nothing is
  // pending): the bind apply's queued rows and the command-channel settle.
  if (Api->settings->bindHasQueued())
    Api->settings->bindVerify();
  if (Api->settings->hasQueued())
    Api->settings->snapshotRows();
  AbcUiVoiceFacts voice{};
  if (voiceFacts(voice) && voice.testing)
    renderVoiceSectionImpl();
  if (Api->settings->revision() != RenderedRevision)
    render();
}

int32_t onEscape(void *, const void *) {
  if (!Page || !Api->documents->visible(Page))
    return 0;
  return requestLeave() ? 1 : 0;
}

} // namespace settingspage
