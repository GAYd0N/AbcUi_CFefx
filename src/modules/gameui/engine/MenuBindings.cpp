// Engine seams extracted from GameUiModule.cpp; no page ownership.
#include <abc/CvarCmd.h>
#include <abc/Plugin.h>
#include "rmlui/InputBridge.h"
#include "rmlui/VGUI2ExtensionImport.h"
#include "data/PlayerResource.h"
#include "rmlui/modelpreview/Options.h"
#include "services/playerlist/MuteBackend.h"
#include <cstring>
#include <string>
#include <string_view>
#include "modules/gameui/engine/MenuBindings.h"
namespace gameui::engine {
namespace {
std::string readCvarString(const char *Name) {
  cvar_t *Cvar = abc::CVAR_GET_POINTER(Name);
  return Cvar && Cvar->string ? std::string(Cvar->string) : std::string();
}
} // namespace

PlayerCardFacts playerCardFacts() {
  PlayerCardFacts Facts;
  Facts.PlayerName = readCvarString("name");
  Facts.ModelName = readCvarString("model");
  // The menu's own override (abc_ui_menu_model_path) wins while it is set;
  // empty follows the player's chosen model, the documented default. The
  // model cvar itself holds the model DIRECTORY under models/player/
  // (Sven Co-op enumerates those folders for its own player-model list)
  // while a value that already names a .mdl is used verbatim - the shared
  // resolver owns both spellings, the never-empty guarantee and the
  // missing-file fallback. Existence is the engine's own filesystem - the
  // same view the Renderer's model loader uses - so a spelling the game
  // cannot load (a model folder that is not installed) shows the stock
  // generic model instead of an empty card.
  Facts.ModelPath = modelpreview::resolveCardModelPath(
      readCvarString("abc_ui_menu_model_path"), Facts.ModelName,
      [](const std::string &Path) {
        return !abc::g_pFileSystem ||
               abc::g_pFileSystem->FileExists(Path.c_str());
      });
  return Facts;
}

std::string currentMapName() {
  const char *level = gEngfuncs.pfnGetLevelName();
  return level ? std::string(level) : std::string();
}

services::EngineUiBackend &menuBackend() {
  static services::EngineUiBackend backend = [] {
    services::EngineUiBackend created;
    services::EngineUiBackend::Seams seams;
    seams.clientCmd = [](const char *command) {
      abc::EngineClientCmd(command);
    };
    // Both engine-UI seams call into the engine's IBaseUI, which is not
    // usable during HUD_Shutdown/ExitGame teardown: the engine unloaded the
    // VGUI modules those outer bodies dereference, and HideConsole forwards
    // to a console object that is already released (NULL dereference - the
    // exit fault UiInputRouter::reset documents). The teardown force-release
    // still lowers every AbcUi truth; only the engine half is skipped, so a
    // live dismissal keeps the vanilla `hideconsole` parity.
    seams.hideGameUi = []() {
      return !rmlui::uiRouter().tearingDown() && vgui2ext::hideGameUi();
    };
    seams.hideConsole = []() {
      return !rmlui::uiRouter().tearingDown() && vgui2ext::hideConsole();
    };
    seams.maxClients = []() { return gEngfuncs.GetMaxClients(); };
    seams.bridge = &services::nativeUiBridge();
    created.setSeams(seams);
    return created;
  }();
  return backend;
}

services::creategame::CreateGameModel::Seams createGameSeams() {
  services::creategame::CreateGameModel::Seams seams;
  seams.readCvar = [](const char *name) -> const char * {
    const cvar_t *cvar = abc::CVAR_GET_POINTER(name);
    return cvar ? cvar->string : nullptr;
  };
  seams.writeCvarString = [](const char *name, const char *value) {
    if (!abc::CVAR_GET_POINTER(name) || !value)
      return false;
    abc::CVAR_SET_STRING(name, value);
    return true;
  };
  seams.clientCmd = [](const char *command) {
    if (!command)
      return;
    abc::EngineClientCmd(command);
  };
  return seams;
}

services::playerlist::PlayerListModel::Seams playerListSeams() {
  services::playerlist::PlayerListModel::Seams seams;
  seams.roster = [](services::playerlist::RosterEntry *out, int max) {
    int count = 0;
    for (int i = 1; i <= playerres::kMaxPlayers && count < max; ++i) {
      const playerres::PlayerInfo *info = playerres::getPlayerInfo(i);
      if (!info || !info->isValid())
        continue;
      services::playerlist::RosterEntry &entry = out[count++];
      entry = services::playerlist::RosterEntry{};
      entry.index = i;
      entry.name = info->name;
      entry.steamId64 = info->steamId64;
      entry.local = info->isThisPlayer();
      entry.spectating = info->spectate;
      // The shipped row fill tagged bots through the engine's "*bot"
      // player-info value (FUN_10015e30 reads PlayerInfo_ValueForKey
      // (index, "*bot") and compares against "1"): the same key through
      // the same engine call, so the classification matches the shipped
      // dialog exactly.
      const char *bot = gEngfuncs.PlayerInfo_ValueForKey(i, "*bot");
      entry.bot = bot && std::string_view(bot) == "1";
    }
    return count;
  };
  seams.muteAvailable = [] {
    return services::playerlist::muteBackend()->available();
  };
  seams.isMuted = [](const services::playerlist::RosterEntry &entry) {
    return services::playerlist::muteBackend()->isMuted(entry.index);
  };
  seams.mute = [](const services::playerlist::RosterEntry &entry) {
    return services::playerlist::muteBackend()->mute(entry.index);
  };
  seams.unmute = [](const services::playerlist::RosterEntry &entry) {
    return services::playerlist::muteBackend()->unmute(entry.index);
  };
  return seams;
}

services::votemenu::VoteMenuModel::Seams voteMenuSeams() {
  services::votemenu::VoteMenuModel::Seams seams;
  seams.roster = [](services::votemenu::PlayerCandidate *out, int max) {
    // The dialog's rows are the same live player resource the scoreboard
    // and player list use. Re-read the engine mirror before the snapshot
    // so a roster change is visible on the same click that opens/refreshes
    // the vote page.
    playerres::updateAll();
    int count = 0;
    for (int i = 1; i <= playerres::kMaxPlayers && count < max; ++i) {
      const playerres::PlayerInfo *info = playerres::getPlayerInfo(i);
      if (!info || !info->isValid())
        continue;
      services::votemenu::PlayerCandidate &entry = out[count++];
      entry = services::votemenu::PlayerCandidate{};
      entry.slot = i;
      entry.name = info->name;
      entry.realName = info->realName;
      entry.steamId64 = info->steamId64;
      entry.local = info->isThisPlayer();
    }
    return count;
  };
  return seams;
}
} // namespace gameui::engine
