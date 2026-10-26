#pragma once

// Shared plumbing for the abcui.gameui page projections: the borrowed api
// table plus the small text/DOM helpers every page repeats. Each page TU
// owns its document, subscriptions and rendering; this header only carries
// what is identical between them (GameUiModule.cpp is the entry: it
// validates the table before any page sees it).

#include <abcui/events.h>
#include <abcui/module_api.h>
#include "../../../common/Parse.h"
#include "../../../common/Text.h"
#include "../../../common/UiText.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

// The framework's table, borrowed once per process and dropped on unload.
inline const AbcUiModuleApi *Api = nullptr;

inline std::optional<int> parseInt(std::string_view text) {
  return moduleui::parseNumber<int>(text);
}

inline std::optional<float> parseFloat(std::string_view text) {
  return moduleui::parseNumber<float>(text);
}

// Bounded copy into an ABI struct's fixed char array: truncate with a
// terminator instead of overrunning the caller's storage.
template <std::size_t N>
void copyCString(char (&dest)[N], std::string_view text) {
  moduleui::copyCString(dest, text);
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
  return moduleui::escapeText(Api, text);
}

inline std::string escapeText(const std::string &text) {
  return escapeText(text.c_str());
}

// Attribute-value variant (data-* payloads carry server-derived text).
// The const char* overload must be declared FIRST: the string form delegates
// to it, and a later declaration would not be visible inside that body (the
// call would resolve to the string overload itself and recurse forever).
inline std::string escapeAttribute(const char *text) {
  return moduleui::escapeAttribute(Api, text);
}

inline std::string escapeAttribute(const std::string &text) {
  return escapeAttribute(text.c_str());
}

inline std::string attributeOf(AbcUiElement *element, const char *name) {
  return moduleui::readString(Api->ui->elementAttribute, element, name);
}
