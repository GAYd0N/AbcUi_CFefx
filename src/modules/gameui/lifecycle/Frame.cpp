// Composition root of frame execution: services first, presentation second.
#include "modules/gameui/GameUiModule.h"
#include "FramePump.h"
#include "services/menumusic/MenuMusic.h"
#include "services/steam/SteamAPI.h"
#include "modules/gameui/engine/MenuBindings.h"
#include <abc/CvarCmd.h>
namespace gameui {
using namespace engine;

void GameUiModule::onFrame(double FrameTime) {
  LastFrameTime = FrameTime;
  // Menu score: plays only while a replacement page is on screen with no
  // level loaded, so the track never bleeds into gameplay (the bridge
  // knows both facts; entering the world ends the track and the in-world
  // ESC menu - a replacement page too - must not bring it back).
  // Playback itself is the engine's mp3 player (MenuMusic drives
  // "mp3 loop <track> ui" / "mp3 stop"); its volume is the audio page's
  // mp3volume row (the engine's own music volume).
  auto &music = menumusic::menuMusic();
  if (cvar_t *source = abc::CVAR_GET_POINTER("abc_ui_menu_music")) {
    if (source->string)
      music.setSource(source->string);
  }
  const auto &bridge = services::nativeUiBridge();
  // A changelevel has no level fact for a few frames (DisconnectFromServer
  // clears it before the new map loads), which would otherwise start the
  // menu track over the Loading page and read as "the server is gone". The
  // map-load attempt suppresses it until LoadingFinished restores the level.
  const auto &state = Controller.state();
  const bool mapLoading = state.phase == menu::BackendPhase::kLoading &&
                          state.attemptKind == menu::AttemptKind::kMapLoad;
  music.update(bridge.pageShown() && !bridge.hasLevel() && !mapLoading);
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
