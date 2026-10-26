#pragma once

#include <charconv>
#include <cmath>
#include <concepts>
#include <optional>
#include <string_view>
#include <system_error>

namespace moduleui {

// Modules cannot depend on AbcCore headers. Keep their strict, untrimmed
// numeric boundary in one place, without widening the C ABI.
template <typename T>
  requires((std::integral<T> && !std::same_as<T, bool>) ||
           std::floating_point<T>)
std::optional<T> parseNumber(std::string_view text) {
  if (text.empty())
    return std::nullopt;
  T value{};
  const char *const end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, value);
  if (result.ec != std::errc{} || result.ptr != end)
    return std::nullopt;
  if constexpr (std::floating_point<T>)
    if (!std::isfinite(value))
      return std::nullopt;
  return value;
}

} // namespace moduleui
