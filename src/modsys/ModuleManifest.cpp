// framework::ModuleManifest implementation - see ModuleManifest.h.
#include "ModuleManifest.h"

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
  out = object[key].GetString();
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
    out.emplace_back(item.GetString());
  }
  return true;
}

// Module ids are lowercase dotted identifiers ([a-z0-9.]); the id is the
// VFS mount key and every cross-module reference resolves through it.
bool validId(const std::string &id) {
  if (id.empty())
    return false;
  for (const char c : id) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.')
      continue;
    return false;
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

  if (!readString(document, "id", out.id, error) ||
      !readString(document, "name", out.name, error) ||
      !readString(document, "description", out.description, error) ||
      !readString(document, "author", out.author, error) ||
      !readString(document, "version", out.version, error) ||
      !readString(document, "minAbcUi", out.minAbcUi, error) ||
      !readString(document, "entry", out.entry, error))
    return false;

  if (!document.HasMember("abi") || !document["abi"].IsInt()) {
    fail(error, "manifest field 'abi' must be an integer");
    return false;
  }
  out.abi = document["abi"].GetInt();

  if (!readIdArray(document, "dependencies", out.dependencies, error) ||
      !readIdArray(document, "conflicts", out.conflicts, error))
    return false;

  if (!validId(out.id)) {
    fail(error, "manifest id must be a lowercase dotted identifier");
    return false;
  }
  if (out.entry.empty()) {
    fail(error, "manifest entry must name the entry DLL inside the package");
    return false;
  }
  return true;
}

int compareVersions(const std::string &left, const std::string &right) {
  size_t leftPos = 0;
  size_t rightPos = 0;
  while (leftPos < left.size() || rightPos < right.size()) {
    const auto readSegment = [](const std::string &text, size_t &pos) {
      int value = 0;
      while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
        value = value * 10 + (text[pos++] - '0');
      if (pos < text.size() && text[pos] == '.')
        ++pos;
      return value;
    };
    const int leftValue =
        leftPos < left.size() ? readSegment(left, leftPos) : 0;
    const int rightValue =
        rightPos < right.size() ? readSegment(right, rightPos) : 0;
    if (leftValue != rightValue)
      return leftValue < rightValue ? -1 : 1;
  }
  return 0;
}

} // namespace framework
