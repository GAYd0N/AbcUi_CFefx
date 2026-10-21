// services::inventory implementation - see InventoryModel.h.
#include "InventoryModel.h"

#include <algorithm>
#include <format>

namespace services::inventory {

namespace {
// Defensive cap only: the shipped inventory is far below this, while a
// misbehaving server must not grow the DOM without bound. The native list
// has no cap of its own.
constexpr std::size_t kMaxItems = 256;
} // namespace

void InventoryModel::add(const netmsg::InvItemData &data) {
  if (Items_.size() >= kMaxItems)
    return;
  Item item;
  item.serial = NextSerial_++;
  item.id = data.id;
  item.canActivate = data.canActivate;
  item.canDrop = data.canDrop;
  item.removeLocked = data.removeLocked;
  // The native window prefers item+0x10 and falls back to item+0x0c.
  item.name = data.displayName.empty() ? data.name : data.displayName;
  item.description = data.description;
  Items_.push_back(std::move(item));
  Revision_++;
}

void InventoryModel::remove(int id, int force) {
  const auto removed = std::remove_if(
      Items_.begin(), Items_.end(), [id, force](const Item &item) {
        // Native MsgFunc_InvRemove: (id == 0 || id == item id) &&
        // (item+0x06 == 0 || force != 0).
        return (id == 0 || id == item.id) && (!item.removeLocked || force != 0);
      });
  if (removed == Items_.end())
    return;
  Items_.erase(removed, Items_.end());
  if (Selected_ != 0 && !selected())
    Selected_ = 0;
  Revision_++;
}

void InventoryModel::clear() {
  if (Items_.empty() && Selected_ == 0)
    return;
  Items_.clear();
  Selected_ = 0;
  Revision_++;
}

const Item *InventoryModel::selected() const {
  if (Selected_ == 0)
    return nullptr;
  for (const Item &item : Items_) {
    if (item.serial == Selected_)
      return &item;
  }
  return nullptr;
}

void InventoryModel::select(std::uint64_t serial) {
  if (serial == Selected_)
    return;
  const bool exists =
      std::any_of(Items_.begin(), Items_.end(),
                  [serial](const Item &item) { return item.serial == serial; });
  Selected_ = exists ? serial : 0;
  Revision_++;
}

bool InventoryModel::moveSelection(int delta) {
  if (Items_.empty() || delta == 0)
    return false;
  if (Selected_ == 0) {
    // No selection yet: the first Down/Up takes the first row instead of
    // jumping over it (the native window had no keyboard walk; this is the
    // page's replacement for the click-to-select step).
    Selected_ = Items_.front().serial;
    Revision_++;
    return true;
  }
  auto itor =
      std::find_if(Items_.begin(), Items_.end(), [this](const Item &item) {
        return item.serial == Selected_;
      });
  if (itor == Items_.end())
    return false;
  const auto index = static_cast<std::ptrdiff_t>(itor - Items_.begin());
  const auto count = static_cast<std::ptrdiff_t>(Items_.size());
  const std::ptrdiff_t next =
      std::clamp(index + static_cast<std::ptrdiff_t>(delta), 0, count - 1);
  if (next == index)
    return false;
  Selected_ = Items_[static_cast<std::size_t>(next)].serial;
  Revision_++;
  return true;
}

std::optional<std::string> InventoryModel::command(Action action) const {
  if (action == Action::kDropAll) {
    if (Items_.empty())
      return std::nullopt;
    return std::string("dropitem \"*\"");
  }
  const Item *item = selected();
  if (!item)
    return std::nullopt;
  switch (action) {
  case Action::kActivate:
    return std::format("activateitem \"#{}\"", item->id);
  case Action::kDrop:
    return std::format("dropitem \"#{}\"", item->id);
  case Action::kDropAll:
    break; // handled above
  }
  return std::nullopt;
}

} // namespace services::inventory
