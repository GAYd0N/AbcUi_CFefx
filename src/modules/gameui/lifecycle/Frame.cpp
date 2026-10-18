// Composition root of frame execution: services first, presentation second.
#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/GameUiPages.h"
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
  music.update(bridge.pageShown() && !bridge.hasLevel());
  // Friends board: the throttle lives in the model, the projection is
  // change-gated on its generation (a steady list writes no DOM).
  if (Shell && Context && Shell->isVisible()) {
    auto &friends = services::friends::friendsModel();
    friends.refresh(FrameTime, steamapi::localSteamId(),
                    playerCardFacts().PlayerName.c_str());
    if (FriendsProjected != friends.generation()) {
      FriendsProjected = friends.generation();
      Shell->setFriends(friends.rows(), friends.available());
    }
  }
  lifecycle::pumpMenuFrame([this] { pumpBackgroundWork(); },
                           [this] { return MenuPresented && Shell && Context; },
                           [this] { pumpVisibleWork(); });
}
} // namespace gameui
