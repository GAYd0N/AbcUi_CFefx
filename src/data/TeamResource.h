#pragma once

// data::teamres - the 22 SC predefined classes/teams and their localize
// tokens. from: ABCEnchance/src/core/resource/teamresource.* (value port;
// the old TeamInfo score-override fields stayed unused by every consumer,
// see the TeamScore note in PlayerResource.h).

#include <array>
#include <cstdint>

namespace teamres {

// Old TEAM_ID enum class (persisted wire values from the ScoreInfo message).
enum class TeamId : int {
  ForceNone = -1,
  None,
  Machine,
  Player,
  HumanPassive,
  HumanMilitary,
  AlienMilitary,
  AlienPassive,
  AlienMonster,
  AlienPrey,
  AlienPredator,
  Insect,
  PlayerAlly,
  PlayerBioweapon,
  AlienBioweapon,
  XracePitdrone,
  XraceShock,
  Team1,
  Team2,
  Team3,
  Team4,
  Barnacle = 99,
};

constexpr size_t kPredefinedTeamCount = 22;

struct PredefinedTeam {
  TeamId id;
  const char *token;
};

// Old g_aryPredefinedTeam table (order = array index of getTeamInfo).
extern const std::array<PredefinedTeam, kPredefinedTeamCount> PredefinedTeams;

// Localize token for a team id ("Team_Unknown" when unlisted) - the old
// GetTeamNameToken.
const char *teamNameToken(TeamId id);

// Index into PredefinedTeams, -1 when unlisted (old GetTeamIndexByTeamID).
int teamIndexByTeamId(TeamId id);

} // namespace teamres
