#pragma once
#include "ServerBlacklist.h"
#include <string_view>

namespace services::serverbrowser {
class BlacklistJson {
public:
  static bool parse(std::string_view Text, ServerBlacklist &Output,
                    std::string &Error);
  static bool serialize(const std::vector<BlacklistRule> &Rules,
                        std::string &Text, std::string &Error);
};
} // namespace services::serverbrowser
