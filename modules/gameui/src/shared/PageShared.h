#pragma once

// Shared plumbing for the abcui.gameui page projections: the borrowed api
// table plus the small text/DOM helpers every page repeats. Each page TU
// owns its document, subscriptions and rendering; this header only carries
// what is identical between them (GameUiModule.cpp is the entry: it
// validates the table before any page sees it).

#include <abcui/events.h>
#include <abcui/module_api.h>

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

// The framework's table, borrowed once per process and dropped on unload.
inline const AbcUiModuleApi *Api = nullptr;

// Strict, locale-independent number parsing (std::from_chars): nullopt on
// empty input, trailing junk or overflow. Module packages cannot include
// the framework's abc::ParseNumber, so this is the local twin.
inline std::optional<int> parseInt(std::string_view text) {
  if (text.empty())
    return std::nullopt;
  int value = 0;
  const char *const end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, value);
  if (result.ec != std::errc{} || result.ptr != end)
    return std::nullopt;
  return value;
}

inline std::optional<float> parseFloat(std::string_view text) {
  if (text.empty())
    return std::nullopt;
  float value = 0.0f;
  const char *const end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, value);
  if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value))
    return std::nullopt;
  return value;
}

// Bounded copy into an ABI struct's fixed char array: truncate with a
// terminator instead of overrunning the caller's storage.
template <std::size_t N>
void copyCString(char (&dest)[N], std::string_view text) {
  const std::size_t count = text.size() < N - 1 ? text.size() : N - 1;
  std::memcpy(dest, text.data(), count);
  dest[count] = '\0';
}

// ui->translate is not a measure-then-copy call (it takes the whole string
// in one shot), so use a fixed table-sized buffer and keep the token when
// the language table does not know it.
inline std::string translate(const char *token) {
  if (!token || !*token)
    return {};
  char out[512];
  return Api->ui->translate(token, out, sizeof(out)) ? std::string(out)
                                                     : std::string(token);
}

// "#Token" resolves through the language table; literal text passes through.
inline std::string resolve(const char *text) {
  if (!text || !*text)
    return {};
  if (*text != '#')
    return text;
  return translate(text + 1);
}

inline std::string escapeText(const char *text) {
  const int needed = Api->ui->escapeRml(text ? text : "", nullptr, 0);
  if (needed <= 0)
    return {};
  std::string out(static_cast<size_t>(needed) + 1, '\0');
  Api->ui->escapeRml(text, out.data(), static_cast<uint32_t>(out.size()));
  out.resize(static_cast<size_t>(needed));
  return out;
}

inline std::string escapeText(const std::string &text) {
  return escapeText(text.c_str());
}

// Attribute-value variant (data-* payloads carry server-derived text).
// The const char* overload must be declared FIRST: the string form delegates
// to it, and a later declaration would not be visible inside that body (the
// call would resolve to the string overload itself and recurse forever).
inline std::string escapeAttribute(const char *text) {
  const int needed = Api->ui->escapeRmlAttribute(text ? text : "", nullptr, 0);
  if (needed <= 0)
    return {};
  std::string out(static_cast<size_t>(needed) + 1, '\0');
  Api->ui->escapeRmlAttribute(text, out.data(),
                              static_cast<uint32_t>(out.size()));
  out.resize(static_cast<size_t>(needed));
  return out;
}

inline std::string escapeAttribute(const std::string &text) {
  return escapeAttribute(text.c_str());
}

inline std::string attributeOf(AbcUiElement *element, const char *name) {
  const int needed = Api->ui->elementAttribute(element, name, nullptr, 0);
  if (needed <= 0)
    return {};
  std::string out(static_cast<size_t>(needed) + 1, '\0');
  Api->ui->elementAttribute(element, name, out.data(),
                            static_cast<uint32_t>(out.size()));
  out.resize(static_cast<size_t>(needed));
  return out;
}
