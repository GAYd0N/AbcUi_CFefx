// gameui::VoteMenuFlow implementation - see VoteMenuFlow.h.
#include "VoteMenuFlow.h"

#include "../../../services/GameUiBridge.h"

#include <abc/Console.h>

namespace gameui {

VoteMenuFlow::VoteMenuFlow(VoteMenuHost host,
                           services::votemenu::VoteMenuModel::Seams seams)
    : Host(std::move(host)) {
  Model.setSeams(seams);
}

void VoteMenuFlow::shutdown() {
  Requested = false;
}

bool VoteMenuFlow::consumeRequest() {
  const bool requested = Requested;
  Requested = false;
  return requested;
}

void VoteMenuFlow::resetForWorld() {
  Model.clearMapList();
  Requested = false;
}

void VoteMenuFlow::open() {
  if (!Host.shellReady()) {
    // The command can arrive in the short window between HUD_Init and Rml
    // assembly. Replay the intent once the context is ready.
    Requested = true;
    return;
  }
  if (!services::nativeUiBridge().hasLevel()) {
    abc::ConsoleWarn(
        "[AbcUi] votemenu is only available while connected to a server\n");
    return;
  }
  if (!Host.ensureMenu() || !Host.reconcileInGame())
    return;
  Host.dispatchOpen();
  Host.finishDispatch();
}

void VoteMenuFlow::applyMapList(int clearAll, int allCount, int start, int end,
                                const std::vector<std::string> &maps) {
  Model.applyMapList(clearAll, allCount, start, end, maps);
}

} // namespace gameui
