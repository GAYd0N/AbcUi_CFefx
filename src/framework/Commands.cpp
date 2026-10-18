// framework::commands implementation - see Commands.h.
#include "Commands.h"

#include <cstdio>
#include <format>
#include <cstring>
#include <memory>
#include <vector>

namespace commands {

namespace {

struct Entry {
  const char *name;
  std::unique_ptr<abc::CChainedCommand> command;
};

std::vector<Entry> &entries() {
  static std::vector<Entry> Entries;
  return Entries;
}

} // namespace

void registerAll() {
  static bool registered = false;
  if (registered)
    return;
  registered = true;
  // Old command.cpp RegisterCommandEvents list. `votemenu` now feeds the
  // AbcUi game-menu vote page; the wire VoteMenu message still drives the
  // separate yes/no HUD panel. `+commandmenu`/`-commandmenu` feed the Rml
  // command page and keep the native VGUI command menu fully suppressed.
  constexpr const char *kHooked[] = {
      "slot1",   "slot2",        "slot3",           "slot4",
      "slot5",   "slot6",        "slot7",           "slot8",
      "slot9",   "slot10",       "missionbriefing", "invnext",
      "invprev", "+showscores",  "-showscores",     "votemenu",
      "+attack", "+commandmenu", "-commandmenu",
  };
  auto &list = entries();
  list.reserve(std::size(kHooked));
  for (const char *name : kHooked)
    list.push_back({name, std::make_unique<abc::CChainedCommand>(name)});
}

abc::CChainedCommand *get(const char *name) {
  for (auto &entry : entries()) {
    if (strcmp(entry.name, name) == 0)
      return entry.command.get();
  }
  return nullptr;
}

abc::CChainedCommand *slotCommand(int index) {
  if (index < 0 || index > 9)
    return nullptr;
  const std::string name = std::format("slot{}", index + 1);
  return get(name.c_str());
}

} // namespace commands

namespace commands {
namespace {
bool TextMenuOpen = false;
}
bool textMenuOpen() {
  return TextMenuOpen;
}
void setTextMenuOpen(bool open) {
  TextMenuOpen = open;
}
} // namespace commands
