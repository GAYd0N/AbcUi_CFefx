#pragma once

// services::uiconfig::shell access - the sdk menuconfig section's access
// point: the parsed shell definition plus the engine facts the shell
// projects. The composition root owns the ConfigRepository and the engine
// seams; the access point is null until it has assembled (never a second
// copy of the definition).

#include <string>

namespace services {
namespace uiconfig {

struct MenuDefinition;

struct MenuShellAccess {
  const MenuDefinition *definition = nullptr;
  // The maxclients split (Main vs Pause rows and their conditions).
  bool (*multiplayer)() = nullptr;
  // The character card's engine facts (player name, Studio model path the
  // preview renders, model label).
  void (*playerCard)(std::string &playerName, std::string &modelPath,
                     std::string &modelLabel) = nullptr;
  // The footer note (the engine build).
  void (*buildNote)(std::string &note) = nullptr;
};

void setResourceRoot(std::string uri);
const std::string &resourceRoot();

void setShellAccess(const MenuShellAccess &access);
const MenuShellAccess &shellAccess();

} // namespace uiconfig
} // namespace services
