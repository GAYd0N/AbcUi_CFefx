#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

namespace avatars {

// Avatar sources may append a cache-busting query, but the identity itself
// must be a complete, nonzero decimal value rather than a numeric prefix.
inline std::optional<std::uint64_t> parseSourceId(std::string_view source,
                                                  std::string_view prefix) {
  if (!source.starts_with(prefix))
    return std::nullopt;
  source.remove_prefix(prefix.size());
  source = source.substr(0, source.find('?'));
  if (source.empty() || source.size() > 20)
    return std::nullopt;
  std::uint64_t id{};
  const char *const end = source.data() + source.size();
  const auto result = std::from_chars(source.data(), end, id);
  if (result.ec != std::errc{} || result.ptr != end || id == 0)
    return std::nullopt;
  return id;
}

} // namespace avatars
