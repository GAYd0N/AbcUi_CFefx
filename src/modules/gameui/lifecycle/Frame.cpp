// Composition root of frame execution: services first, presentation second.
#include "modules/gameui/GameUiModule.h"
#include "FramePump.h"
#include "services/steam/SteamAPI.h"
#include "modules/gameui/engine/MenuBindings.h"
#include <abc/CvarCmd.h>
namespace gameui {
using namespace engine;

void GameUiModule::onFrame(double FrameTime) {
  if (!services::nativeUiBridge().armed())
    return;
  LastFrameTime = FrameTime;
  // Friends board: the throttle lives in the model; the module page
  // projects the rows change-gated on the model's generation (the sdk
  // social section). The refresh runs while a home page is on top - the
  // controller truth of the module shell being visible.
  if (Context && MenuPresented &&
      (Controller.state().pages.topIs(MenuPage::kMain) ||
       Controller.state().pages.topIs(MenuPage::kPause))) {
    services::friends::friendsModel().refresh(
        FrameTime, steamapi::localSteamId(),
        playerCardFacts().PlayerName.c_str());
  }
  lifecycle::pumpMenuFrame(
      [this] { pumpBackgroundWork(); },
      [this] { return MenuPresented && Context != nullptr; },
      [this] { pumpVisibleWork(); });
  // The presentation beat (menu.paint): page modules project their DOM here,
  // inside the framework's takeover pipeline. menu.state stays edge-only.
  if (MenuPresented)
    publishMenuPresentation();
}
} // namespace gameui
