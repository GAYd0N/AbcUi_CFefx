#pragma once
#include "ServerBlacklist.h"
#include <filesystem>

namespace services::serverbrowser {
class BlacklistFileStore {
public:
  static bool load(const std::filesystem::path &Path, ServerBlacklist &Output,
                   std::string &Error);
  static bool save(const std::filesystem::path &Path,
                   const std::vector<BlacklistRule> &Rules, std::string &Error);
};
} // namespace services::serverbrowser
