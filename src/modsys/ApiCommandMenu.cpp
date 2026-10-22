// framework::CommandMenuApi - the sdk commandmenu section (the in-game
// command menu's model and navigation). See ApiTable.h for how it joins the
// module table.
#include "ApiTable.h"

#include <algorithm>
#include <cstring>

#include "../services/commandmenu/CommandMenuModel.h"
#include "../services/commandmenu/CommandMenuService.h"

namespace framework {
namespace CommandMenuApi {
namespace {

// The projection window the page shows: three consecutive levels at once
// (the old page's kVisibleColumns, now owned by this seam so the page
// cannot disagree with the sliding-window math).
constexpr std::size_t kWindow = 3;

void copyText(const std::string &text, char *out, uint32_t outSize) {
  if (!out || outSize == 0)
    return;
  uint32_t i = 0;
  for (; i + 1 < outSize && i < text.size(); ++i)
    out[i] = text[i];
  out[i] = '\0';
}

services::commandmenu::Model *model() {
  return services::commandmenu::access().model;
}

// A command closes the menu first and then runs the engine command; a
// close lowers the page with the flow's raised-menu discipline. Both
// happen here so the page only re-projects.
int32_t finish(const services::commandmenu::ActionResult &result) {
  using Kind = services::commandmenu::ActionResult::Kind;
  const services::commandmenu::Access &seam = services::commandmenu::access();
  switch (result.kind) {
  case Kind::kCommand:
    if (seam.submitCommand)
      seam.submitCommand(result.command.c_str());
    return ABCUI_CMDMENU_RESULT_COMMAND;
  case Kind::kClose:
    if (seam.dismiss)
      seam.dismiss();
    return ABCUI_CMDMENU_RESULT_CLOSE;
  case Kind::kSubmenu:
    return ABCUI_CMDMENU_RESULT_SUBMENU;
  case Kind::kNone:
    break;
  }
  return ABCUI_CMDMENU_RESULT_NONE;
}

uint32_t apiRevision(void) {
  const auto *target = model();
  return target ? static_cast<uint32_t>(target->revision()) : 0;
}

const char *apiTitle(void) {
  const auto *target = model();
  return target ? target->definition().Title.c_str() : "";
}

int32_t apiColumnCount(void) {
  const auto *target = model();
  return target ? static_cast<int32_t>(target->columnCount()) : 0;
}

int32_t apiFirstVisibleColumn(void) {
  const auto *target = model();
  return target ? static_cast<int32_t>(target->firstVisibleColumn(kWindow)) : 0;
}

int32_t apiVisibleColumnCount(void) {
  const auto *target = model();
  return target ? static_cast<int32_t>(target->visibleColumnCount(kWindow)) : 0;
}

int32_t apiItemCountAt(int32_t column) {
  const auto *target = model();
  if (!target || column < 0 ||
      static_cast<size_t>(column) >= target->columnCount())
    return 0;
  return static_cast<int32_t>(
      target->itemsAt(static_cast<size_t>(column)).size());
}

int32_t apiSelectedAt(int32_t column) {
  const auto *target = model();
  if (!target || column < 0 ||
      static_cast<size_t>(column) >= target->columnCount())
    return 0;
  return static_cast<int32_t>(target->selectedAt(static_cast<size_t>(column)));
}

int32_t apiItemAt(int32_t column, int32_t index, AbcUiCommandMenuItem *out) {
  const auto *target = model();
  if (!target || !out || column < 0 || index < 0 ||
      static_cast<size_t>(column) >= target->columnCount())
    return 0;
  const auto &items = target->itemsAt(static_cast<size_t>(column));
  if (static_cast<size_t>(index) >= items.size())
    return 0;
  const services::commandmenu::Item &item = *items[static_cast<size_t>(index)];

  AbcUiCommandMenuItem full{};
  full.structSize = sizeof(AbcUiCommandMenuItem);
  full.hasItems = item.hasItems() ? 1 : 0;
  copyText(item.Label, full.label, sizeof(full.label));

  // The caller's declared size wins: never write past an older header.
  const uint32_t declared = out->structSize == 0
                                ? static_cast<uint32_t>(sizeof(full))
                                : out->structSize;
  std::memcpy(out, &full, std::min<size_t>(declared, sizeof(full)));
  return 1;
}

int32_t apiMoveSelection(int32_t delta) {
  auto *target = model();
  return target && target->moveSelection(delta) ? 1 : 0;
}

int32_t apiOpenSelected(void) {
  auto *target = model();
  return target ? finish(target->openSelected()) : ABCUI_CMDMENU_RESULT_NONE;
}

int32_t apiActivateSelected(void) {
  auto *target = model();
  return target ? finish(target->activateSelected())
                : ABCUI_CMDMENU_RESULT_NONE;
}

int32_t apiActivateAt(int32_t column, int32_t index) {
  auto *target = model();
  if (!target || column < 0 || index < 0)
    return ABCUI_CMDMENU_RESULT_NONE;
  return finish(target->activateAt(static_cast<size_t>(column),
                                   static_cast<size_t>(index)));
}

int32_t apiActivateShortcut(int32_t digit) {
  auto *target = model();
  if (!target || digit < 0 || digit > 9)
    return ABCUI_CMDMENU_RESULT_NONE;
  return finish(target->activateShortcut(static_cast<char>('0' + digit)));
}

int32_t apiBack(void) {
  auto *target = model();
  return target ? finish(target->back()) : ABCUI_CMDMENU_RESULT_NONE;
}

void apiDismiss(void) {
  const services::commandmenu::Access &seam = services::commandmenu::access();
  if (seam.dismiss)
    seam.dismiss();
}

const AbcUiCommandMenuApi kCommandMenuApi = {
    sizeof(AbcUiCommandMenuApi),
    apiRevision,
    apiTitle,
    apiColumnCount,
    apiFirstVisibleColumn,
    apiVisibleColumnCount,
    apiItemCountAt,
    apiSelectedAt,
    apiItemAt,
    apiMoveSelection,
    apiOpenSelected,
    apiActivateSelected,
    apiActivateAt,
    apiActivateShortcut,
    apiBack,
    apiDismiss,
};

} // namespace

const AbcUiCommandMenuApi *get() {
  return &kCommandMenuApi;
}

} // namespace CommandMenuApi
} // namespace framework
