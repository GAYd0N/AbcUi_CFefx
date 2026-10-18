// services::playerlist::PlayerListModel implementation - see PlayerListModel.h
// for the identity/mute contracts and the shipped-behaviour evidence.
#include "PlayerListModel.h"

#include <algorithm>

namespace services {
namespace playerlist {

SelectionKey SelectionKey::forEntry(const RosterEntry &entry) {
  SelectionKey key;
  if (entry.steamId64 != 0) {
    key.HasId = true;
    key.SteamId64 = entry.steamId64;
    return key;
  }
  // No stable ID: the key is slot+name scoped. Honest about its weakness -
  // name and slot both have to match, so a same-slot rename (or a slot
  // reused by another ID-less player) invalidates the selection.
  key.HasId = false;
  key.Index = entry.index;
  key.Name = entry.name;
  return key;
}

bool SelectionKey::matches(const RosterEntry &entry) const {
  if (HasId)
    return entry.steamId64 != 0 && entry.steamId64 == SteamId64;
  // An ID-less key never matches an ID-bearing row (the key types are
  // tagged; a placeholder row must not be confusable with a real ID).
  if (entry.steamId64 != 0)
    return false;
  return !entry.name.empty() && entry.index == Index && entry.name == Name;
}

bool PlayerListModel::sameOccupancy(const std::vector<RosterEntry> &a,
                                    const std::vector<RosterEntry> &b) {
  if (a.size() != b.size())
    return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].index != b[i].index || a[i].steamId64 != b[i].steamId64)
      return false;
    // A slot reused by a DIFFERENT ID-less player is an occupancy change
    // (that is the only identity such a row has).
    if (a[i].steamId64 == 0 && a[i].name != b[i].name)
      return false;
  }
  return true;
}

void PlayerListModel::rebuildRoster() {
  std::vector<RosterEntry> next;
  if (Seams_.roster) {
    RosterEntry buffer[64];
    const int count = Seams_.roster(buffer, 64);
    const int capped = count > 64 ? 64 : (count < 0 ? 0 : count);
    next.assign(buffer, buffer + capped);
  }
  std::sort(next.begin(), next.end(),
            [](const RosterEntry &a, const RosterEntry &b) {
              return a.index < b.index;
            });

  const bool occupancyChanged = !Primed || !sameOccupancy(Roster, next);
  if (occupancyChanged)
    ++Generation;
  Primed = true;
  Roster = std::move(next);
}

RosterEntry *PlayerListModel::findEntry(const SelectionKey &key) {
  for (RosterEntry &entry : Roster) {
    if (key.matches(entry))
      return &entry;
  }
  return nullptr;
}

bool PlayerListModel::refresh() {
  const std::vector<RosterEntry> before = Roster;
  const std::uint32_t generationBefore = Generation;
  const bool availabilityBefore = MuteAvailable;
  const bool selectionBeforeValid = Selection.valid();

  MuteAvailable = Seams_.muteAvailable && Seams_.muteAvailable();
  rebuildRoster();

  // Mute states are read through the backend every refresh; never cached as
  // truth beyond this snapshot.
  for (RosterEntry &entry : Roster) {
    entry.muted = MuteAvailable && Seams_.isMuted && Seams_.isMuted(entry);
  }

  // Selection survival: the identity must still occupy the snapshot. A
  // vanished identity clears the selection (T20.b step 2: 玩家离开时取消
  // 过期选择); a still-present identity keeps it even across a slot move
  // (SteamID-bearing keys are slot-independent).
  if (selectionBeforeValid && findEntry(Selection) == nullptr)
    Selection = SelectionKey{};

  bool changed =
      Generation != generationBefore || MuteAvailable != availabilityBefore;
  if (!changed && Roster.size() == before.size()) {
    for (std::size_t i = 0; i < Roster.size(); ++i) {
      if (Roster[i].name != before[i].name ||
          Roster[i].muted != before[i].muted ||
          Roster[i].local != before[i].local ||
          Roster[i].spectating != before[i].spectating ||
          Roster[i].bot != before[i].bot) {
        changed = true;
        break;
      }
    }
  }
  if (changed)
    ++Revision;
  return changed;
}

void PlayerListModel::select(const SelectionKey &key) {
  if (!key.valid())
    return;
  if (findEntry(key) == nullptr)
    return; // a key without a live occupant is refused, not remembered
  Selection = key;
  ++Revision;
}

void PlayerListModel::clearSelection() {
  if (!Selection.valid())
    return;
  Selection = SelectionKey{};
  ++Revision;
}

const RosterEntry *PlayerListModel::selectedEntry() const {
  for (const RosterEntry &entry : Roster) {
    if (Selection.matches(entry))
      return &entry;
  }
  return nullptr;
}

ToggleError PlayerListModel::toggleMuted(bool &mutedAfter) {
  mutedAfter = false;
  const bool HadSelection = Selection.valid();
  // A rendered snapshot is not authorization for a later slot operation.
  refresh();
  if (!MuteAvailable)
    return ToggleError::kUnavailable;
  if (!Selection.valid())
    return HadSelection ? ToggleError::kIdentityChanged
                        : ToggleError::kNoSelection;
  // Revalidate against the LIVE snapshot at action time: the page may have
  // rendered an older generation (disconnect/reconnect/map change between
  // render and click must never retarget the action).
  RosterEntry *entry = findEntry(Selection);
  if (entry == nullptr)
    return ToggleError::kIdentityChanged;
  if (entry->bot)
    return ToggleError::kIdentityChanged; // shipped rule: bots are not mutable
  const bool wasMuted = Seams_.isMuted ? Seams_.isMuted(*entry) : false;
  const bool accepted = wasMuted
                            ? (Seams_.unmute ? Seams_.unmute(*entry) : false)
                            : (Seams_.mute ? Seams_.mute(*entry) : false);
  if (!accepted)
    return ToggleError::kReadbackFailed;
  // Read back: success is claimed only when the backend's own state agrees.
  mutedAfter = Seams_.isMuted ? Seams_.isMuted(*entry) : false;
  if (mutedAfter == wasMuted)
    return ToggleError::kReadbackFailed; // the toggle did not stick
  entry->muted = mutedAfter;
  ++Revision;
  return ToggleError::kNone;
}

} // namespace playerlist
} // namespace services
