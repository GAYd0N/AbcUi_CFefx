// framework::commands implementation - see Commands.h.
#include "Commands.h"

#include <cstdio>
#include <format>
#include <cstring>
#include <memory>
#include <vector>

#include "../modsys/EventBus.h"

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
  // `messagemode`/`messagemode2` belong to the chat panel (issue 13): the
  // bare invocation opens the Rml input line, an argumented one chains.
  // `inventory`/`+inventory`/`-inventory` feed the Rml inventory window
  // (issue 10) and keep the native CInventoryPanel fully suppressed.
  constexpr const char *kHooked[] = {
      "slot1",        "slot2",        "slot3",           "slot4",
      "slot5",        "slot6",        "slot7",           "slot8",
      "slot9",        "slot10",       "missionbriefing", "invnext",
      "invprev",      "+showscores",  "-showscores",     "votemenu",
      "+attack",      "+commandmenu", "-commandmenu",    "messagemode",
      "messagemode2", "inventory",    "+inventory",      "-inventory",
  };
  auto &list = entries();
  list.reserve(std::size(kHooked));
  for (const char *name : kHooked) {
    auto command = std::make_unique<abc::CChainedCommand>(name);
    // Module bridge: every hooked command mirrors onto an aggregate veto
    // bus channel ("command.<name>"). Aggregate = the old CallbackList
    // semantics, every subscriber runs and independently decides whether
    // the engine's original path runs. CChainedCommand chains the original
    // only when every subscriber returned true, while the bus reports
    // "consumed" (any sink returned nonzero), so the two invert.
    command->Event().append([name]() {
      const std::string channel = std::string("command.") + name;
      return !framework::eventBus().publishVetoAggregateRaw(channel.c_str(),
                                                            nullptr);
    });
    list.push_back({name, std::move(command)});
  }
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
