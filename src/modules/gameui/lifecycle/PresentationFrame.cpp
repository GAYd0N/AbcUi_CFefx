// Extracted from GameUiModule.cpp (UTF-8). Responsibility: lifecycle/Frame.
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Debugger.h>
#include "modules/gameui/GameUiModule.h"
#include "modsys/ModuleDocuments.h"
namespace gameui {
void GameUiModule::pumpVisibleWork() {
  // The settings page's model beats (bind verification, the kCommandToggle
  // settle, the mic-test meter) run page-side from the sdk settings section
  // on the same paint beat, so the composition root no longer reaches into
  // the page.
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
    // Only a VISIBLE module document holds legitimate focus - a hidden
    // one (an overlay just dismissed) is exactly the stale state this
    // guard exists to repair.
    if (owner && owner->IsVisible() &&
        framework::module_documents::ownsVisible(owner))
      return;
  }
  // T12: the display-mode confirmation outranks every other surface - it is
  // a visible page module document that owns its own focus, so the guard
  // never steals it while the confirmation is up.
  if (Video->confirmPresented())
    return;
  // Every surface is a module document now, and each one restores its own
  // safe default when its layer closes (the settings/bind/quit paths already
  // do); the framework only guards against a hidden owner keeping focus.
}
} // namespace gameui
