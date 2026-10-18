#pragma once

// framework::commands - the chained command bus (batch C). The old
// src/core/events/command.cpp hooked slot1..10, missionbriefing, invnext,
// invprev, +/-showscores, votemenu and +attack with HOOK_COMMAND wrappers;
// here every hooked command is one abc::CChainedCommand instance and panels
// subscribe its Event() with the bool-consume callback shape. Instances are
// plugin-lifetime (fixed slot table in abc/Events.h) and must be created at
// HUD_Init - registerAll() from PanelManager::registerPanelCvars.
//
// TextMenu created slot1..10 locally in batch A; batch C moves them here so
// WeaponChoose/Scoreboard subscribe the same instances instead of double-
// hooking the commands (a second HOOK_COMMAND on one name would shadow).

#include <abc/Events.h>

namespace commands {

// Creates every chained command instance (idempotent; HUD_Init time only).
void registerAll();

// The instance for a hooked command ("slot3", "+showscores", ...); nullptr
// when the name is not on the bus (typo guard).
abc::CChainedCommand *get(const char *name);
// Convenience: the slotN chain for a zero-based slot index.
abc::CChainedCommand *slotCommand(int index);

} // namespace commands

namespace commands {
// Main-thread interaction state; does not depend on a concrete panel header.
bool textMenuOpen();
void setTextMenuOpen(bool open);
} // namespace commands
