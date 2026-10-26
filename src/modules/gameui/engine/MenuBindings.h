#pragma once
// Internal MenuBindings seam; no engine SDK exposed.
#include "services/EngineUiBackend.h"
#include "services/creategame/CreateGameModel.h"
#include "services/playerlist/PlayerListModel.h"
#include "services/votemenu/VoteMenuModel.h"
namespace gameui::engine {
services::EngineUiBackend &menuBackend();
services::creategame::CreateGameModel::Seams createGameSeams();
services::playerlist::PlayerListModel::Seams playerListSeams();
services::votemenu::VoteMenuModel::Seams voteMenuSeams();

// The character card's facts, read from the engine (the model/name cvars the
// player edits in Settings > Multiplayer). Empty strings when unregistered.
struct PlayerCardFacts {
  std::string PlayerName;
  std::string ModelName; // as the cvar spells it ("HT_Cerberus")
  std::string ModelPath; // resolvable Studio path for the <model> element
  // The action group that drives the card's pose, the weapon merged onto it
  // and the group sequence shown (empty path = that entity is not used).
  std::string ActionModel;
  std::string WeaponModel;
  int Sequence = 0;
};
PlayerCardFacts playerCardFacts();
// Current engine level name; empty when no world is loaded. The command-menu
// model normalizes the path and extension for optional per-map entries.
std::string currentMapName();
} // namespace gameui::engine
