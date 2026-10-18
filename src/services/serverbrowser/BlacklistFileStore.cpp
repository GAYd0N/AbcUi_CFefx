#include "BlacklistFileStore.h"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <fstream>
#include <set>
#ifdef _WIN32
#include <windows.h>
#endif

namespace services::serverbrowser {
bool BlacklistFileStore::load(const std::filesystem::path &Path,
                              ServerBlacklist &Output, std::string &Error) {
  std::error_code EC;
  if (!std::filesystem::exists(Path, EC) && !EC)
    return true;
  std::ifstream File(Path, std::ios::binary | std::ios::ate);
  if (!File || File.tellg() < 0 || File.tellg() > 1024 * 1024) {
    Error = "blacklist unreadable or too large";
    return false;
  }
  std::string Text(static_cast<size_t>(File.tellg()), '\0');
  File.seekg(0);
  if (!Text.empty() && !File.read(Text.data(), Text.size())) {
    Error = "blacklist read failed";
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
bool BlacklistFileStore::save(const std::filesystem::path &Path,
                              const std::vector<BlacklistRule> &Rules,
                              std::string &Error) {
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
    const bool ValidName = NameWriter.String(Rule.Name.data(), Length);
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
  std::error_code EC;
  if (!Path.parent_path().empty())
    std::filesystem::create_directories(Path.parent_path(), EC);
  auto Temporary = Path;
  Temporary += ".tmp";
  std::ofstream File(Temporary, std::ios::binary | std::ios::trunc);
  if (EC || !File || !File.write(Buffer.GetString(), Buffer.GetSize()) ||
      !File.flush()) {
    Error = "cannot write blacklist";
    return false;
  }
  File.close();
  if (!File) {
    Error = "cannot close blacklist";
    return false;
  }
#ifdef _WIN32
  if (!MoveFileExW(Temporary.c_str(), Path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    Error = "cannot replace blacklist (" + std::to_string(GetLastError()) + ")";
    return false;
  }
#else
  std::filesystem::rename(Temporary, Path, EC);
  if (EC) {
    Error = EC.message();
    return false;
  }
#endif
  return true;
}
} // namespace services::serverbrowser
