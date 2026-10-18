#pragma once
// Settings resource discovery and owned parsed-table storage. No DOM or input.
#include "services/settings/SettingsModel.h"
#include <cstddef>
#include <string>
#include <vector>
namespace gameui::settings {
std::string readModFile(const char *relativePath);
// Writes a mod-relative binary file through the engine filesystem (the mod
// directory is writable; the engine's own tempdecal.wad lives there). Falls
// back to a direct file beside the mod path. False + error on any failure.
bool writeModFile(const char *relativePath,
                  const std::vector<unsigned char> &bytes, std::string &error);
std::vector<std::string> scanModDirectories(const char *relativeDir);
std::vector<std::string> scanModFileBasenames(const char *relativeDir);
// Files directly under <mod>/<relativeDir> whose extension matches one of
// `extensions` (case-insensitive, ".wav" form), sorted by value. The value is
// <valuePrefix><name>; the label is the name without its extension. Raw
// filesystem walk of the mod's own tree: the scanned folders are where users
// drop media, and the engine search paths would list addon/download roots as
// duplicates the player cannot manage from here.
std::vector<services::settings::StringChoice>
scanModMediaFiles(const char *relativeDir, const char *const *extensions,
                  int extensionCount, const char *valuePrefix);
services::settings::SettingsModel::UserScrTable loadAdvancedScript();
} // namespace gameui::settings
