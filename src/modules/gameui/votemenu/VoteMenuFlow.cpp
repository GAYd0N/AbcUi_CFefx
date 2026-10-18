// gameui::VoteMenuFlow implementation - see VoteMenuFlow.h.
#include "VoteMenuFlow.h"

#include "../../../panels/votemenu/VoteMenuPage.h"
#include "../../../services/GameUiBridge.h"

#include <abc/Console.h>
#include <abc/CvarCmd.h>

namespace gameui {

VoteMenuFlow::VoteMenuFlow(VoteMenuHost host,
                           services::votemenu::VoteMenuModel::Seams seams)
    : Host(std::move(host)) {
  Model.setSeams(seams);
}

void VoteMenuFlow::setPage(panels::votemenu::VoteMenuPage *page) {
  Page = page;
  if (Page)
    Page->setModel(&Model);
}

void VoteMenuFlow::shutdown() {
  Page = nullptr;
  Presented = false;
  Requested = false;
}

bool VoteMenuFlow::consumeRequest() {
  const bool requested = Requested;
  Requested = false;
  return requested;
}

void VoteMenuFlow::resetForWorld() {
  Model.clearMapList();
  hide();
  Requested = false;
}

void VoteMenuFlow::hide() {
  if (Page)
    Page->hide();
  Presented = false;
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

  const bool freshOpening = !Host.pageOnTop();
  if (freshOpening && Page) {
    // The original dialog reopened on its Ban page. Keep the server map
    // catalogue, but reset tabs/filter/selection for this opening.
    Page->prepare();
  }
  Host.dispatchOpen();
  Host.finishDispatch();
}

void VoteMenuFlow::present() {
  if (!Page)
    return;
  Model.refresh();
  Page->render();
  Page->show();
  Presented = true;
}

void VoteMenuFlow::tick() {
  if (!Presented || !Page)
    return;
  Model.refresh();
  Page->render();
}

void VoteMenuFlow::applyMapList(int clearAll, int allCount, int start, int end,
                                const std::vector<std::string> &maps) {
  Model.applyMapList(clearAll, allCount, start, end, maps);
  if (Presented && Page)
    Page->render();
}

void VoteMenuFlow::handleAction(navigation::ActionKind kind) {
  using navigation::ActionKind;
  if (kind == ActionKind::kVoteMenuCancel) {
    Host.dispatchBack();
    Host.finishDispatch();
    return;
  }
  if (kind != ActionKind::kVoteMenuSubmit) {
    abc::ConsoleWarn("[AbcUi] unknown vote-menu action \"%s\" dropped\n",
                     navigation::actionName(kind));
    return;
  }

  std::string command;
  const char *errorToken = nullptr;
  if (!Model.submitCommand(command, errorToken)) {
    if (Page) {
      Page->render();
      Page->showHint(errorToken);
    }
    return;
  }
  // ABCEnchance sent these verbs through pfnServerCmd; keep that wire path.
  abc::ServerCmd(command.c_str());
  Host.dispatchBack();
  Host.finishDispatch();
}

} // namespace gameui
