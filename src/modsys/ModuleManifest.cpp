// framework::ModuleManifest implementation - see ModuleManifest.h.
#include "ModuleManifest.h"

#include <algorithm>
#include <initializer_list>
#include <string_view>
#include <utility>

#include <rapidjson/document.h>

namespace framework {
namespace {

void fail(std::string *error, const std::string &message) {
  if (error)
    *error = message;
}

bool readString(const rapidjson::Value &object, const char *key,
                std::string &out, std::string *error) {
  if (!object.HasMember(key) || !object[key].IsString()) {
    fail(error, std::string("manifest field '") + key + "' must be a string");
    return false;
  }
  out.assign(object[key].GetString(), object[key].GetStringLength());
  if (out.find('\0') != std::string::npos) {
    fail(error, std::string("manifest field '") + key +
                    "' must not contain null characters");
    return false;
  }
  return true;
}

bool readIdArray(const rapidjson::Value &object, const char *key,
                 std::vector<std::string> &out, std::string *error) {
  if (!object.HasMember(key))
    return true; // optional
  const rapidjson::Value &array = object[key];
  if (!array.IsArray()) {
    fail(error, std::string("manifest field '") + key + "' must be an array");
    return false;
  }
  for (const auto &item : array.GetArray()) {
    if (!item.IsString()) {
      fail(error, std::string("manifest field '") + key +
                      "' must contain only strings");
      return false;
    }
    out.emplace_back(item.GetString(), item.GetStringLength());
  }
  return true;
}

// Module ids are non-empty lowercase dotted identifier segments; the id is the
// VFS mount key and every cross-module reference resolves through it.
bool validId(const std::string &id) {
  if (id.empty() || id.front() == '.' || id.back() == '.')
    return false;
  bool previousDot = false;
  for (const char c : id) {
    if (c == '.' && previousDot)
      return false;
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.'))
      return false;
    previousDot = c == '.';
  }
  return true;
}

bool validVersion(std::string_view version) {
  bool needDigit = true;
  for (const char c : version) {
    if (c >= '0' && c <= '9') {
      needDigit = false;
    } else if (c == '.' && !needDigit) {
      needDigit = true;
    } else {
      return false;
    }
  }
  return !needDigit;
}

bool validEntry(std::string &entry) {
  std::replace(entry.begin(), entry.end(), '\\', '/');
  if (entry.empty() || entry.find(':') != std::string::npos)
    return false;
  size_t pos = 0;
  while (pos <= entry.size()) {
    const size_t end = entry.find('/', pos);
    const std::string_view segment(
        entry.data() + pos,
        (end == std::string::npos ? entry.size() : end) - pos);
    if (segment.empty() || segment == "." || segment == "..")
      return false;
    if (end == std::string::npos)
      break;
    pos = end + 1;
  }
  return true;
}

} // namespace

bool parseManifest(const char *text, size_t size, ModuleManifest &out,
                   std::string *error) {
  rapidjson::Document document;
  document.Parse(text, size);
  if (document.HasParseError()) {
    fail(error, "manifest is not valid JSON");
    return false;
  }
  if (!document.IsObject()) {
    fail(error, "manifest must be a JSON object");
    return false;
  }

  ModuleManifest parsed;
  if (!readString(document, "id", parsed.id, error) ||
      !readString(document, "name", parsed.name, error) ||
      !readString(document, "description", parsed.description, error) ||
      !readString(document, "author", parsed.author, error) ||
      !readString(document, "version", parsed.version, error) ||
      !readString(document, "minAbcUi", parsed.minAbcUi, error))
    return false;
  if (document.HasMember("type")) {
    std::string type;
    if (!readString(document, "type", type, error))
      return false;
    if (type != "module" && type != "resources") {
      fail(error, "manifest type must be 'module' or 'resources'");
      return false;
    }
    parsed.resourcesOnly = type == "resources";
  }
  if (parsed.resourcesOnly) {
    if (document.HasMember("entry")) {
      fail(error, "resource packages must not declare an entry");
      return false;
    }
  } else if (!readString(document, "entry", parsed.entry, error)) {
    return false;
  }

  if (!document.HasMember("abi") || !document["abi"].IsInt()) {
    fail(error, "manifest field 'abi' must be an integer");
    return false;
  }
  parsed.abi = document["abi"].GetInt();
  if (parsed.abi <= 0) {
    fail(error, "manifest abi must be positive");
    return false;
  }

  if (!readIdArray(document, "dependencies", parsed.dependencies, error) ||
      !readIdArray(document, "conflicts", parsed.conflicts, error))
    return false;

  if (!validId(parsed.id)) {
    fail(error, "manifest id must be a lowercase dotted identifier");
    return false;
  }
  for (const auto *ids : {&parsed.dependencies, &parsed.conflicts}) {
    for (const std::string &id : *ids) {
      if (!validId(id)) {
        fail(error, "manifest dependencies/conflicts must contain module ids");
        return false;
      }
    }
  }
  if (!validVersion(parsed.version) || !validVersion(parsed.minAbcUi)) {
    fail(error,
         "manifest versions must contain decimal segments separated by dots");
    return false;
  }
  if (!parsed.resourcesOnly && !validEntry(parsed.entry)) {
    fail(error,
         "manifest entry must be a relative package path without dot segments");
    return false;
  }
  out = std::move(parsed);
  return true;
}

int compareVersions(const std::string &left, const std::string &right) {
  size_t leftPos = 0;
  size_t rightPos = 0;
  while (leftPos < left.size() || rightPos < right.size()) {
    const auto readSegment = [](const std::string &text, size_t &pos) {
      const size_t begin = pos;
      const size_t end = text.find('.', pos);
      pos = end == std::string::npos ? text.size() : end + 1;
      std::string_view value(text.data() + begin,
                             (end == std::string::npos ? text.size() : end) -
                                 begin);
      while (!value.empty() && value.front() == '0')
        value.remove_prefix(1);
      return value;
    };
    const std::string_view leftValue =
        leftPos < left.size() ? readSegment(left, leftPos) : std::string_view{};
    const std::string_view rightValue = rightPos < right.size()
                                            ? readSegment(right, rightPos)
                                            : std::string_view{};
    if (leftValue.size() != rightValue.size())
      return leftValue.size() < rightValue.size() ? -1 : 1;
    if (leftValue != rightValue)
      return leftValue < rightValue ? -1 : 1;
  }
  return 0;
}

} // namespace framework
