#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace moduleui {

template <std::size_t N>
void copyCString(char (&dest)[N], std::string_view text) {
  static_assert(N > 0);
  const auto count = (std::min)(text.size(), N - 1);
  if (count)
    std::copy_n(text.begin(), count, dest);
  dest[count] = '\0';
}

// Replace each original occurrence once, including adjacent occurrences.
// The views must not refer to storage owned by text.
inline void replaceAll(std::string &text, std::string_view from,
                       std::string_view to) {
  if (from.empty())
    return;
  std::size_t at = 0;
  while ((at = text.find(from, at)) != std::string::npos) {
    text.replace(at, from.size(), to);
    at += to.size();
  }
}

} // namespace moduleui
