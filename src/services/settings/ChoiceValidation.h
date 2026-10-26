#pragma once

#include <algorithm>
#include <string_view>

namespace services::settings {

// Model/option names are display tokens, never paths or console fragments.
inline bool choiceTokenValid(std::string_view text) {
  return !text.empty() && text != "." && text != ".." &&
         text.find_first_of("\"'`\\;/|<>*?[]:") == text.npos &&
         std::ranges::none_of(
             text, [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

} // namespace services::settings
