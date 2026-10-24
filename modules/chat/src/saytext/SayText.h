#pragma once

// chatsaytext - the SayText wire parser: the server's colour control bytes
// (0x01 text, 0x02 legacy name marker, 0x03 name, 0x04 location) become
// ordered runs, the client's positional %s/%sN parameters are substituted and
// the leading (TEAM)/*DEAD*/*SPEC* decoration is split off. Pure string work:
// no api table, no DOM, no module state, so the parser can be reasoned about
// (and exercised) on its own.

#include <string>
#include <vector>

namespace chatsaytext {

enum class RunKind { kText = 0, kName, kPrefix, kLocation };

struct Run {
  std::string text;
  RunKind kind = RunKind::kText;
};

std::vector<Run> buildRuns(const std::string &text, const char *const params[4],
                           const std::string &senderName);

} // namespace chatsaytext
