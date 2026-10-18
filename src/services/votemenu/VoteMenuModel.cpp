#include "VoteMenuModel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace services::votemenu {

namespace {

constexpr std::size_t kMaxPlayers = 64;
constexpr std::size_t kMaxMaps = 4096;
constexpr std::size_t kMaxArgument = 256;

std::string lowerAscii(std::string text) {
  for (char &c : text) {
    const unsigned char value = static_cast<unsigned char>(c);
    if (value < 0x80)
      c = static_cast<char>(std::tolower(value));
  }
  return text;
}

const char *commandFor(Kind kind) {
  switch (kind) {
  case Kind::kBan:
    return "voteban";
  case Kind::kKick:
    return "votekick";
  case Kind::kKill:
    return "votekill";
  case Kind::kMap:
    return "votemap";
  }
  return "";
}

} // namespace

const char *kindId(Kind value) {
  switch (value) {
  case Kind::kBan:
    return "ban";
  case Kind::kKick:
    return "kick";
  case Kind::kKill:
    return "kill";
  case Kind::kMap:
    return "map";
  }
  return "ban";
}

bool parseKind(std::string_view text, Kind &out) {
  if (text == "ban")
    out = Kind::kBan;
  else if (text == "kick")
    out = Kind::kKick;
  else if (text == "kill")
    out = Kind::kKill;
  else if (text == "map")
    out = Kind::kMap;
  else
    return false;
  return true;
}

void VoteMenuModel::clearSelection() {
  Selected = Selection{};
}

void VoteMenuModel::select(std::size_t visibleIndex) {
  if (visibleIndex >= Visible.size()) {
    clearSelection();
    return;
  }
  const Candidate &candidate = Visible[visibleIndex];
  Selected.kind = Kind_;
  Selected.slot = candidate.slot;
  Selected.value = candidate.value;
  ++Revision;
}

bool VoteMenuModel::selectionMatches(const Candidate &candidate) const {
  return Selected.kind == Kind_ && candidate.kind == Kind_ &&
         Selected.slot == candidate.slot && Selected.value == candidate.value;
}

const Candidate *VoteMenuModel::selected() const {
  for (const Candidate &candidate : Visible) {
    if (selectionMatches(candidate))
      return &candidate;
  }
  return nullptr;
}

void VoteMenuModel::rebuild() {
  Visible.clear();
  const std::string needle = lowerAscii(Filter_);
  if (Kind_ == Kind::kMap) {
    std::vector<std::string> sorted = Maps;
    std::sort(sorted.begin(), sorted.end());
    for (const std::string &map : sorted) {
      if (!needle.empty() && lowerAscii(map).find(needle) == std::string::npos)
        continue;
      Candidate candidate;
      candidate.kind = Kind_;
      candidate.name = map;
      candidate.value = map;
      Visible.push_back(std::move(candidate));
    }
  } else {
    for (const PlayerCandidate &player : Players) {
      if (!needle.empty()) {
        const bool matches =
            lowerAscii(player.name).find(needle) != std::string::npos ||
            lowerAscii(player.realName).find(needle) != std::string::npos;
        if (!matches)
          continue;
      }
      Candidate candidate;
      candidate.kind = Kind_;
      candidate.slot = player.slot;
      candidate.name = player.name;
      candidate.realName = player.realName;
      if (player.steamId64 != 0)
        candidate.steamId = std::to_string(player.steamId64);
      candidate.value = player.name;
      Visible.push_back(std::move(candidate));
    }
  }
  if (!selected())
    clearSelection();
  ++Revision;
}

void VoteMenuModel::prepare() {
  Kind_ = Kind::kBan;
  Filter_.clear();
  clearSelection();
  refresh();
  rebuild();
}

bool VoteMenuModel::sameRoster(const std::vector<PlayerCandidate> &a,
                               const std::vector<PlayerCandidate> &b) {
  if (a.size() != b.size())
    return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].slot != b[i].slot || a[i].name != b[i].name ||
        a[i].realName != b[i].realName || a[i].steamId64 != b[i].steamId64 ||
        a[i].local != b[i].local)
      return false;
  }
  return true;
}

bool VoteMenuModel::refresh() {
  std::vector<PlayerCandidate> next;
  if (Seams_.roster) {
    PlayerCandidate buffer[kMaxPlayers];
    int count = Seams_.roster(buffer, static_cast<int>(kMaxPlayers));
    if (count < 0)
      count = 0;
    if (static_cast<std::size_t>(count) > kMaxPlayers)
      count = static_cast<int>(kMaxPlayers);
    next.assign(buffer, buffer + count);
  }
  std::sort(next.begin(), next.end(),
            [](const PlayerCandidate &a, const PlayerCandidate &b) {
              return a.slot < b.slot;
            });
  const bool changed = !Primed || !sameRoster(Players, next);
  if (!changed)
    return false;
  Primed = true;
  Players = std::move(next);
  if (selected() == nullptr)
    clearSelection();
  rebuild();
  return true;
}

void VoteMenuModel::applyMapList(int clearAll, int allCount, int start, int end,
                                 const std::vector<std::string> &maps) {
  (void)start;
  if (clearAll == 0) {
    Maps.clear();
    if (allCount > 0) {
      const std::size_t advertised = static_cast<std::size_t>(allCount);
      Maps.reserve(advertised < kMaxMaps ? advertised : kMaxMaps);
    }
  }
  if (end < start)
    return;
  const std::size_t incoming = static_cast<std::size_t>(end - start);
  for (std::size_t i = 0; i < incoming && i < maps.size(); ++i) {
    const std::string &map = maps[i];
    if (map.empty() || Maps.size() >= kMaxMaps)
      continue;
    if (std::find(Maps.begin(), Maps.end(), map) != Maps.end())
      continue;
    Maps.push_back(map);
  }
  if (selected() == nullptr)
    clearSelection();
  rebuild();
}

void VoteMenuModel::clearMapList() {
  if (Maps.empty())
    return;
  Maps.clear();
  if (selected() == nullptr)
    clearSelection();
  rebuild();
}

void VoteMenuModel::setKind(Kind value) {
  if (Kind_ == value)
    return;
  Kind_ = value;
  Filter_.clear();
  clearSelection();
  rebuild();
}

void VoteMenuModel::setFilter(std::string value) {
  if (Filter_ == value)
    return;
  Filter_ = std::move(value);
  clearSelection();
  rebuild();
}

bool VoteMenuModel::safeCommandArgument(const std::string &value) {
  if (value.empty() || value.size() > kMaxArgument)
    return false;
  for (const unsigned char c : value) {
    if (c < 0x20 || c == 0x7f || c == ';')
      return false;
  }
  return true;
}

bool VoteMenuModel::submitCommand(std::string &command,
                                  const char *&errorToken) {
  command.clear();
  errorToken = nullptr;
  refresh();
  if (selected() == nullptr) {
    errorToken = "#VoteMenu_NoSelection";
    return false;
  }
  // The selection may be visible from a stale render; revalidate against
  // the live snapshot once more before constructing the command.
  const Candidate *candidate = selected();
  if (!candidate || !safeCommandArgument(candidate->value)) {
    errorToken = "#VoteMenu_InvalidEntry";
    return false;
  }
  command = commandFor(Kind_);
  command.push_back(' ');
  command += candidate->value;
  return true;
}

} // namespace services::votemenu
