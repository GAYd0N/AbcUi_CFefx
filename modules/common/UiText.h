#pragma once

#include <abcui/module_api.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace moduleui {

// Only for byte-count-returning measure/copy APIs, not boolean translate().
// A changed size or failed copy must not publish partial RML.
template <typename Read, typename... Args>
  requires std::same_as<std::invoke_result_t<Read, Args..., char *, uint32_t>,
                        int32_t>
std::string readString(Read read, Args... args) {
  const auto needed = read(args..., nullptr, uint32_t{0});
  if (needed <= 0)
    return {};
  std::string out(static_cast<std::size_t>(needed) + 1, '\0');
  if (read(args..., out.data(), static_cast<uint32_t>(out.size())) != needed)
    return {};
  out.resize(static_cast<std::size_t>(needed));
  return out;
}

inline std::string escapeText(const AbcUiModuleApi *api, const char *text) {
  return text && *text ? readString(api->ui->escapeRml, text) : std::string();
}

inline std::string escapeText(const AbcUiModuleApi *api,
                              const std::string &text) {
  return escapeText(api, text.c_str());
}

inline std::string escapeText(const AbcUiModuleApi *api,
                              std::string_view text) {
  // A view's data() need not be terminated, especially for a substring.
  return escapeText(api, std::string(text));
}

inline std::string escapeAttribute(const AbcUiModuleApi *api,
                                   const char *text) {
  return text && *text ? readString(api->ui->escapeRmlAttribute, text)
                       : std::string();
}

inline std::string escapeAttribute(const AbcUiModuleApi *api,
                                   std::string_view text) {
  return escapeAttribute(api, std::string(text).c_str());
}

} // namespace moduleui
