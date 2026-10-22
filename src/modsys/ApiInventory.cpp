// framework::InventoryApi - the sdk inventory section (the item window's
// model and actions). See ApiTable.h for how it joins the module table.
#include "ApiTable.h"

#include <algorithm>
#include <cstring>

#include "../services/inventory/InventoryModel.h"
#include "../services/inventory/InventoryService.h"

#include <abc/CvarCmd.h>

namespace framework {
namespace InventoryApi {
namespace {

void copyText(const std::string &text, char *out, uint32_t outSize) {
  if (!out || outSize == 0)
    return;
  uint32_t i = 0;
  for (; i + 1 < outSize && i < text.size(); ++i)
    out[i] = text[i];
  out[i] = '\0';
}

services::inventory::InventoryModel *model() {
  return services::inventory::access().model;
}

uint32_t apiRevision(void) {
  const auto *target = model();
  return target ? static_cast<uint32_t>(target->revision()) : 0;
}

int32_t apiItemCount(void) {
  const auto *target = model();
  return target ? static_cast<int32_t>(target->items().size()) : 0;
}

int32_t apiItemAt(int32_t index, AbcUiInvItem *out) {
  const auto *target = model();
  if (!target || !out || index < 0)
    return 0;
  const auto &items = target->items();
  if (static_cast<size_t>(index) >= items.size())
    return 0;
  const services::inventory::Item &item = items[static_cast<size_t>(index)];

  AbcUiInvItem full{};
  full.structSize = sizeof(AbcUiInvItem);
  full.serial = item.serial;
  full.selected = target->selectedSerial() == item.serial ? 1 : 0;
  full.canActivate = item.canActivate ? 1 : 0;
  full.canDrop = item.canDrop ? 1 : 0;
  copyText(item.name, full.name, sizeof(full.name));
  copyText(item.description, full.description, sizeof(full.description));

  // The caller's declared size wins: never write past an older header.
  const uint32_t declared = out->structSize == 0
                                ? static_cast<uint32_t>(sizeof(full))
                                : out->structSize;
  std::memcpy(out, &full, std::min<size_t>(declared, sizeof(full)));
  return 1;
}

void apiSelect(uint64_t serial) {
  if (auto *target = model())
    target->select(serial);
}

int32_t apiMoveSelection(int32_t delta) {
  auto *target = model();
  return target && target->moveSelection(delta) ? 1 : 0;
}

int32_t apiActivate(int32_t action) {
  auto *target = model();
  if (!target)
    return 0;
  services::inventory::Action verb;
  switch (action) {
  case ABCUI_INV_ACTION_ACTIVATE:
    verb = services::inventory::Action::kActivate;
    break;
  case ABCUI_INV_ACTION_DROP:
    verb = services::inventory::Action::kDrop;
    break;
  case ABCUI_INV_ACTION_DROP_ALL:
    verb = services::inventory::Action::kDropAll;
    break;
  default:
    return 0;
  }
  const std::optional<std::string> command = target->command(verb);
  if (!command)
    return 0;
  // Native order: the window closes, then the queued client command runs.
  abc::EngineClientCmd(command->c_str());
  const services::inventory::Access &seam = services::inventory::access();
  if (seam.dismiss)
    seam.dismiss();
  return 1;
}

void apiDismiss(void) {
  const services::inventory::Access &seam = services::inventory::access();
  if (seam.dismiss)
    seam.dismiss();
}

const AbcUiInventoryApi kInventoryApi = {
    sizeof(AbcUiInventoryApi), apiRevision, apiItemCount, apiItemAt, apiSelect,
    apiMoveSelection,          apiActivate, apiDismiss,
};

} // namespace

const AbcUiInventoryApi *get() {
  return &kInventoryApi;
}

} // namespace InventoryApi
} // namespace framework
