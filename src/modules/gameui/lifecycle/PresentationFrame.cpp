// Extracted from GameUiModule.cpp (UTF-8). Responsibility: lifecycle/Frame.
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Debugger.h>
#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/GameUiPages.h"
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
  // T16 console stream: while the overlay is presented, the page
  // appends the buffer's new lines once per frame (bounded incremental
  // projection; no-op when nothing new arrived).
  if (ConsolePresented && Console)
    Console->render();
  // Vote page: the server roster can change while the page is up. The page
  // rebuilds only when the model revision moved.
  Vote->tick();
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
  Rml::Element *inputEditor = nullptr;
  if (!Video->confirmPresented()) {
    if (state.overlays.topIs(MenuOverlay::kPassword) && Password)
      inputEditor = Password->editor();
    else if (state.overlays.topIs(MenuOverlay::kDirectConnect) && DirectConnect)
      inputEditor = DirectConnect->editor();
  }
  if (focus && (!inputEditor || focus == inputEditor)) {
    Rml::ElementDocument *owner = focus->GetOwnerDocument();
    // Only a VISIBLE menu document holds legitimate focus - a hidden
    // one (stub/overlay just dismissed) is exactly the stale state
    // this guard exists to repair.
    if (owner && owner->IsVisible() &&
        (owner == Shell->document() ||
         owner == (SubStub ? SubStub->document() : nullptr) ||
         owner == (Console ? Console->document() : nullptr) ||
         owner == (Settings ? Settings->document() : nullptr) ||
         owner == (Browser ? Browser->document() : nullptr) ||
         owner == (CreateGamePage_ ? CreateGamePage_->document() : nullptr) ||
         owner == (PlayerListPage_ ? PlayerListPage_->document() : nullptr) ||
         owner == (VoteMenuPage_ ? VoteMenuPage_->document() : nullptr) ||
         owner == (ConfirmQuit ? ConfirmQuit->document() : nullptr) ||
         owner == (VideoConfirm ? VideoConfirm->document() : nullptr) ||
         owner == (Loading ? Loading->document() : nullptr) ||
         owner == (ConnError ? ConnError->document() : nullptr) ||
         owner == (Password ? Password->document() : nullptr) ||
         owner == (DirectConnect ? DirectConnect->document() : nullptr)))
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
  else if (state.overlays.contains(MenuOverlay::kDirectConnect) &&
           DirectConnect)
    DirectConnect->refocusDefault();
  else if (state.overlays.contains(MenuOverlay::kPassword) && Password)
    Password->refocusDefault();
  else if (state.overlays.contains(MenuOverlay::kConnectionError) && ConnError)
    ConnError->refocusDefault();
  else if (state.overlays.contains(MenuOverlay::kConsole) && Console)
    Console->refocusDefault();
  else if (!state.pages.empty() && Shell) {
    if (state.pages.top() == MenuPage::kMain ||
        state.pages.top() == MenuPage::kPause) {
      Shell->showSection(state.pages.top() == MenuPage::kMain
                             ? panels::mainmenu::MainMenuPage::Section::kMain
                             : panels::mainmenu::MainMenuPage::Section::kPause,
                         true);
    } else if (state.pages.top() == MenuPage::kSettings && Settings) {
      Settings->refocusDefault();
    } else if (state.pages.top() == MenuPage::kServers && Browser) {
      Browser->refocusDefault();
    } else if (state.pages.top() == MenuPage::kCreateGame && CreateGamePage_) {
      CreateGamePage_->refocusDefault();
    } else if (state.pages.top() == MenuPage::kPlayerList && PlayerListPage_) {
      PlayerListPage_->refocusDefault();
    } else if (state.pages.top() == MenuPage::kVoteMenu && VoteMenuPage_) {
      VoteMenuPage_->refocusDefault();
    } else if (state.pages.top() == MenuPage::kInventory && InventoryPage_) {
      InventoryPage_->refocusDefault();
    } else if (state.pages.top() == MenuPage::kLoading && Loading) {
      Loading->refocusDefault();
    } else if (SubStub) {
      SubStub->refocusDefault();
    }
  }
}
} // namespace gameui
