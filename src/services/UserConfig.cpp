#include "UserConfig.h"

#include <abc/Config.h>
#include <abc/Console.h>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <cstring>
#include <set>
#include <string>

namespace services::userconfig {
namespace {

constexpr const char *kPath = "abc/abcui/config.json";
struct State {
  rapidjson::Document document;
  bool initialized = false;
  bool readable = false;
};

State &state() {
  static State instance;
  return instance;
}

bool parse(const std::string &text, rapidjson::Document &document) {
  document.Parse<rapidjson::kParseValidateEncodingFlag |
                 rapidjson::kParseIterativeFlag>(text.data(), text.size());
  if (text.find('\0') != std::string::npos || document.HasParseError() ||
      !document.IsObject())
    return false;
  // Duplicate keys would make a section update ambiguous.
  std::set<std::string> keys;
  for (auto member = document.MemberBegin(); member != document.MemberEnd();
       ++member)
    if (!keys.emplace(member->name.GetString(), member->name.GetStringLength())
             .second)
      return false;
  return true;
}

bool serialize(const rapidjson::Value &value, std::string &text) {
  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer, rapidjson::UTF8<>,
                          rapidjson::UTF8<>, rapidjson::CrtAllocator,
                          rapidjson::kWriteValidateEncodingFlag>
      writer(buffer);
  if (!value.Accept(writer))
    return false;
  text.assign(buffer.GetString(), buffer.GetSize());
  return true;
}

bool persist(const rapidjson::Document &document) {
  std::string text;
  if (!serialize(document, text) || !abc::WriteJsonFile(kPath, text)) {
    abc::ConsoleWarn("[AbcUi] could not save %s\n", kPath);
    return false;
  }
  return true;
}

bool refresh() {
  auto &s = state();
  std::string text;
  const auto status = abc::ReadJsonFileStatus(kPath, text);
  if (status == abc::JsonLoadStatus::Success) {
    rapidjson::Document document;
    if (!parse(text, document)) {
      abc::ConsoleWarn("[AbcUi] invalid %s; refusing to overwrite it\n", kPath);
      s.readable = false;
      return false;
    }
    s.document.Swap(document);
    s.initialized = s.readable = true;
    return true;
  }
  if (status != abc::JsonLoadStatus::Missing) {
    abc::ConsoleWarn("[AbcUi] unreadable %s; refusing to overwrite it\n",
                     kPath);
    s.readable = false;
    return false;
  }
  if (s.initialized)
    return s.readable;
  s.initialized = s.readable = true;
  s.document.SetObject();
  auto &allocator = s.document.GetAllocator();
  s.document.AddMember("render_backend", "renderer", allocator);
  persist(s.document);
  return true;
}

} // namespace

LoadStatus readSection(const char *name, std::string &json) {
  if (!name || !*name || !refresh())
    return LoadStatus::Unreadable;
  const auto &s = state();
  const auto member = s.document.FindMember(name);
  if (member == s.document.MemberEnd())
    return LoadStatus::Missing;
  if (!member->value.IsObject() || !serialize(member->value, json))
    return LoadStatus::Unreadable;
  return LoadStatus::Loaded;
}

bool writeSection(const char *name, const std::string &json) {
  if (!name || !*name || std::strcmp(name, "render_backend") == 0 || !refresh())
    return false;
  auto &s = state();
  rapidjson::Document section;
  if (!parse(json, section))
    return false;
  rapidjson::Document next;
  next.CopyFrom(s.document, next.GetAllocator());
  auto &allocator = next.GetAllocator();
  rapidjson::Value value(section, allocator);
  const auto member = next.FindMember(name);
  if (member == next.MemberEnd()) {
    rapidjson::Value key(name, allocator);
    next.AddMember(key, value, allocator);
  } else {
    member->value.Swap(value);
  }
  if (!persist(next))
    return false;
  s.document.Swap(next);
  return true;
}

bool renderBackend(std::string &id) {
  if (!refresh())
    return false;
  const auto &document = state().document;
  const auto member = document.FindMember("render_backend");
  if (member == document.MemberEnd()) {
    id = "renderer";
    return true;
  }
  if (!member->value.IsString() || !member->value.GetStringLength() ||
      member->value.GetStringLength() > 64)
    return false;
  const std::string selected(member->value.GetString(),
                             member->value.GetStringLength());
  for (char c : selected)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
      return false;
  id = selected;
  return true;
}

} // namespace services::userconfig
