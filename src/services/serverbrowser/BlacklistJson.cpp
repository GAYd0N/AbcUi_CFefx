#include "BlacklistJson.h"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <set>

namespace services::serverbrowser {
bool BlacklistJson::parse(std::string_view Text, ServerBlacklist &Output,
                          std::string &Error) {
  if (Text.size() > 1024 * 1024) {
    Error = "blacklist too large";
    return false;
  }
  rapidjson::Document Doc;
  Doc.Parse<rapidjson::kParseValidateEncodingFlag |
            rapidjson::kParseIterativeFlag>(Text.data(), Text.size());
  if (Text.find('\0') != std::string::npos || Doc.HasParseError() ||
      !Doc.IsObject() || !Doc.HasMember("version") || !Doc["version"].IsInt() ||
      Doc["version"].GetInt() != 1 || !Doc.HasMember("rules") ||
      !Doc["rules"].IsArray() || Doc["rules"].Size() > 1024) {
    Error = "invalid blacklist document";
    return false;
  }
  std::vector<BlacklistRule> Rules;
  std::set<std::string> Seen;
  for (const auto &Item : Doc["rules"].GetArray()) {
    BlacklistRule Rule;
    if (!Item.IsObject() || !Item.HasMember("address") ||
        !Item["address"].IsString() ||
        !ServerBlacklist::parseAddress(
            std::string(Item["address"].GetString(),
                        Item["address"].GetStringLength()),
            Rule)) {
      Error = "invalid blacklist address";
      return false;
    }
    if (Item.HasMember("name")) {
      if (!Item["name"].IsString() || Item["name"].GetStringLength() > 512) {
        Error = "invalid blacklist name";
        return false;
      }
      Rule.Name.assign(Item["name"].GetString(),
                       Item["name"].GetStringLength());
    }
    // Optional: files written before the timestamp existed load as
    // "unknown" (0) instead of inventing a date.
    if (Item.HasMember("blockedAt")) {
      if (!Item["blockedAt"].IsUint64() ||
          Item["blockedAt"].GetUint64() > 4102444800ull) {
        Error = "invalid blacklist timestamp";
        return false;
      }
      Rule.BlockedAt = Item["blockedAt"].GetUint64();
    }
    if (Seen.insert(Rule.address()).second)
      Rules.push_back(std::move(Rule));
  }
  Output.restore(std::move(Rules));
  return true;
}
bool BlacklistJson::serialize(const std::vector<BlacklistRule> &Rules,
                              std::string &Text, std::string &Error) {
  if (Rules.size() > 1024) {
    Error = "too many blacklist rules";
    return false;
  }
  rapidjson::StringBuffer Buffer;
  rapidjson::Writer<rapidjson::StringBuffer> Writer(Buffer);
  Writer.StartObject();
  Writer.Key("version");
  Writer.Int(1);
  Writer.Key("rules");
  Writer.StartArray();
  for (const auto &Rule : Rules) {
    Writer.StartObject();
    Writer.Key("address");
    Writer.String(Rule.address().c_str());
    // A hostile server's malformed display name must not poison the
    // persisted address rules. Keep the rule, omit only its invalid label.
    rapidjson::StringBuffer NameBuffer;
    rapidjson::Writer<rapidjson::StringBuffer, rapidjson::UTF8<>,
                      rapidjson::UTF8<>, rapidjson::CrtAllocator,
                      rapidjson::kWriteValidateEncodingFlag>
        NameWriter(NameBuffer);
    const auto Length = static_cast<rapidjson::SizeType>(Rule.Name.size());
    const bool ValidName =
        Length <= 512 && NameWriter.String(Rule.Name.data(), Length);
    Writer.Key("name");
    Writer.String(ValidName ? Rule.Name.data() : "", ValidName ? Length : 0);
    if (Rule.BlockedAt != 0) {
      Writer.Key("blockedAt");
      Writer.Uint64(Rule.BlockedAt);
    }
    Writer.EndObject();
  }
  Writer.EndArray();
  Writer.EndObject();
  Text.assign(Buffer.GetString(), Buffer.GetSize());
  return true;
}
} // namespace services::serverbrowser
