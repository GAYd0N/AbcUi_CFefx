#pragma once

// services::creategame::access - the sdk creategame section's access point:
// the process-lifetime model plus the flow's apply boundary. The model (the
// map catalog, the server options and the kept request used by local retry)
// stays framework-side; the page module submits the form's fields once on
// Apply and this seam assembles, validates and dispatches them.

#include <string>
#include <utility>
#include <vector>

namespace services {
namespace creategame {

class CreateGameModel;

struct Access {
  CreateGameModel *model = nullptr;
  // The flow's apply boundary: reads the fixed fields ("map", "players",
  // "name", "password", "lan") plus one entry per option. Returns 1 when
  // the attempt dispatched; 0 on refusal with a hint token/text.
  bool (*submit)(const std::vector<std::pair<std::string, std::string>> &fields,
                 std::string &hint) = nullptr;
};

void setAccess(const Access &access);
const Access &access();

} // namespace creategame
} // namespace services
