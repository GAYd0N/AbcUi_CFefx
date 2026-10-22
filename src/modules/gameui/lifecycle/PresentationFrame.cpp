// Extracted from GameUiModule.cpp (UTF-8). Responsibility: lifecycle/Frame.
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Debugger.h>
#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/GameUiPages.h"
#include "modsys/ModuleDocuments.h"
namespace gameui {
void GameUiModule::pumpVisibleWork() {

  // T11 bind verification beat: pfnClientCmd lands in the engine
  // command buffer, which flushes between frames. kQueued rows resolve
  // on the first tick after Apply; a change re-projects the page once.
  if (SettingsView->presented() && Settings && BindModel.hasQueued()) {
    if (BindModel.verify())
      Settings->render();
  }
  // T13: the same beat covers the cvar model's kCommandToggle rows
  // (+mlook/-mlook settle against IsKeyDown) and the mic-test meter
  // (the level readout is the GetSpeakingVolume truth, one render per
  // frame while the session is up - not an animation).
  if (SettingsView->presented() && Settings &&
      (SettingsModel.hasQueued() || VoiceModel.testing())) {
    if (SettingsModel.hasQueued())
      SettingsModel.snapshot();
    Settings->renderVoiceSection();
    if (SettingsModel.hasQueued())
      Settings->render();
  }
  // Per-frame focus guard (T09 step 5): while the menu is presented,
  // the context focus must stay inside one of the menu documents.
  // Foreign holders exist (the Rml debug hook document grabs the focus
  // whenever the last menu document hides - live-verified), and a
  // sync-time guard alone loses to thieves that act between frames.
  // The debugger's own windows are exempt (a developer using F12 must
  // keep their focus).
  if (Rml::Debugger::IsVisible())
    return; // a developer using the F12 tools keeps their focus
  Rml::Element *focus = Context->GetFocusElement();
  const auto &state = Controller.state();
  // T14 input dialogs own the keyboard while they are the top-most layer:
  // only the dialog's text editor is a legitimate focus holder there. A
  // click on 连接/取消 focuses the button (RmlUi's mousedown default); the
  // text input handler demands a focused editor, so every character typed
  // afterwards is dropped and the next Connect click would submit an
  // empty field - the live "password retyped, the connection never
  // continues" defect. Falling through re-focuses the editor instead.
  // The T14 dialogs own their editors (module documents): a click on a
  // dialog button focuses the button and the next keystroke would drop -
  // the bitmask test defers the guard to that layer without reaching into
  // its DOM.
  const bool inputDialogUp =
      !Video->confirmPresented() &&
      (state.overlays.topIs(MenuOverlay::kPassword) ||
       state.overlays.topIs(MenuOverlay::kDirectConnect));
  if (focus && !inputDialogUp) {
    Rml::ElementDocument *owner = focus->GetOwnerDocument();
    // Only a VISIBLE menu document holds legitimate focus - a hidden
    // one (stub/overlay just dismissed) is exactly the stale state
    // this guard exists to repair.
    if (owner && owner->IsVisible() &&
        (framework::module_documents::ownsVisible(owner) ||
         owner == (SubStub ? SubStub->document() : nullptr) ||
         owner == (Settings ? Settings->document() : nullptr) ||
         owner == (ConfirmQuit ? ConfirmQuit->document() : nullptr) ||
         owner == (VideoConfirm ? VideoConfirm->document() : nullptr)))
      return;
  }
  // T12: the display-mode confirmation outranks every other surface -
  // it holds the safe default focus while it is up.
  if (Video->confirmPresented() && VideoConfirm) {
    VideoConfirm->refocusDefault();
    return;
  }
  // T14 surfaces, top-most first (the same order the sync shows them).
  if (state.overlays.contains(MenuOverlay::kConfirmQuit) && ConfirmQuit)
    ConfirmQuit->refocusDefault();
  else if (state.overlays.contains(MenuOverlay::kDirectConnect) ||
           state.overlays.contains(MenuOverlay::kPassword) ||
           state.overlays.contains(MenuOverlay::kConnectionError)) {
    // The connection dialogs are module documents: each owns its field
    // focus (the guard leaves a visible module document alone above).
  } else if (state.overlays.contains(MenuOverlay::kConsole)) {
    // The console page is a module document: it owns its editor focus.
  } else if (!state.pages.empty()) {
    if (state.pages.top() == MenuPage::kMain ||
        state.pages.top() == MenuPage::kPause) {
      // The menu home is a module document: it owns its entry focus (the
      // guard leaves a visible module document alone above).
    } else if (state.pages.top() == MenuPage::kSettings && Settings) {
      Settings->refocusDefault();
    } else if (state.pages.top() == MenuPage::kServers) {
      // The browser page is a module document: it owns its focus.
    } else if (state.pages.top() == MenuPage::kLoading) {
      // The loading page is a module document: it owns its Cancel focus.
    } else if (SubStub) {
      SubStub->refocusDefault();
    }
  }
}
} // namespace gameui
