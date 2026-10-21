#pragma once

// services::inventory - the item list the server streams (issue 10).
//
// The native CInventoryPanel keeps a linked list fed by the InvAdd and
// InvRemove user messages (client.dll: MsgFunc_InvAdd/MsgFunc_InvRemove ->
// CInventoryPanel::AddItem/RemoveItem) and renders it on demand. This model
// mirrors that list from the same wire events instead of reading the client
// object: the panel is still constructed by the viewport (and kept hidden by
// the command hook), but its layout is private to the client build, while
// the messages are the contract the server actually sends.
//
// Read-through rule: actions (activate/drop/drop-all, native
// "%sitem \"#%d\"" / "dropitem \"*\"") are sent once and never mutate the
// list. A removed item disappears only when the server's InvRemove arrives -
// no optimistic erase, so a refused action keeps the window truthful.
//
// Selection is presentation state, keyed by the model's monotonic serial
// (the server id is not unique - the native list tolerates duplicate ids).
// Removing the selected item clears the selection; any other removal keeps
// it. The native window's selection survives a rebuild by its private key
// string; the serial gives the same stability without the client struct.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../../data/NetworkMessages.h"

namespace services::inventory {

enum class Action {
  kActivate,
  kDrop,
  kDropAll,
};

struct Item {
  std::uint64_t serial = 0; // model-local identity (never sent)
  int id = 0;               // server id used by the action commands
  bool canActivate = false;
  bool canDrop = false;
  bool removeLocked = false;
  std::string name;        // display name (native item+0x10, else +0x0c)
  std::string description; // native item+0x14
};

class InventoryModel {
public:
  // InvAdd: append in arrival order (native AddItem appends to the tail).
  // The display name prefers the native window's second string.
  void add(const netmsg::InvItemData &data);
  // InvRemove: id 0 removes every unlocked item; a non-zero force also
  // removes items the server had marked removeLocked (native rule).
  void remove(int id, int force);
  void clear();

  const std::vector<Item> &items() const {
    return Items_;
  }
  bool empty() const {
    return Items_.empty();
  }
  // Bumped by every list or selection change; the page re-projects only on
  // a new revision.
  std::uint64_t revision() const {
    return Revision_;
  }

  std::uint64_t selectedSerial() const {
    return Selected_;
  }
  const Item *selected() const;
  // Selects an existing serial; an unknown serial clears the selection.
  void select(std::uint64_t serial);
  // Keyboard walk: selects the first item when nothing is selected, then
  // clamps at the ends. True when the selection moved.
  bool moveSelection(int delta);

  // The client command for an action, or nullopt when the action has no
  // target (no selection, or drop-all on an empty list). The caller sends it
  // through abc::EngineClientCmd and closes the window.
  std::optional<std::string> command(Action action) const;

private:
  std::vector<Item> Items_;
  std::uint64_t Selected_ = 0;
  std::uint64_t NextSerial_ = 1;
  std::uint64_t Revision_ = 1;
};

} // namespace services::inventory
