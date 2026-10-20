// services::voice::VoiceBanStore implementation - see the header.
#include "VoiceBanStore.h"

#include <AIGCJson.hpp>

#include <abc/Config.h>
#include <abc/Console.h>

#include <algorithm>
#include <string>

namespace services {
namespace voice {

namespace {
constexpr const char *kStorePath = "abc/abcui/voice_ban.dt";
// The scoreboard renders one row per slot; a list beyond this is a damaged
// store rather than an achievement.
constexpr size_t kMaxBans = 1024;

// The on-disk schema. Field order defines the written key order only; the
// parser is key-based (a missing key keeps the default).
struct VoiceBanJson {
  std::vector<std::string> Bans;
  AIGC_JSON_HELPER(Bans)
  AIGC_JSON_HELPER_RENAME("bans")
};

// Canonical decimal SteamID64: digits only, no leading zeros (except "0"
// itself, refused below - the zero id is the "no identity" sentinel),
// representable in uint64.
bool idTextValid(const std::string &text) {
  if (text.empty() || text.size() > 20)
    return false;
  if (text.size() > 1 && text[0] == '0')
    return false;
  for (const char c : text) {
    if (c < '0' || c > '9')
      return false;
  }
  const std::uint64_t value = std::strtoull(text.c_str(), nullptr, 10);
  return value != 0;
}

std::uint64_t idFromText(const std::string &text) {
  return std::strtoull(text.c_str(), nullptr, 10);
}

std::string textFromId(std::uint64_t id) {
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "%llu",
                static_cast<unsigned long long>(id));
  return buffer;
}
} // namespace

VoiceBanStore::LoadStatus VoiceBanStore::load(std::vector<std::uint64_t> &out) {
  VoiceBanJson json;
  const auto status = abc::JsonLoadWithStatus(json, kStorePath);
  if (status == abc::JsonLoadStatus::Missing) {
    out.clear();
    return LoadStatus::kMissing;
  }
  if (status != abc::JsonLoadStatus::Success) {
    abc::ConsoleWarn("[AbcUi] voice ban store unreadable (%d)\n",
                     static_cast<int>(status));
    out.clear();
    return LoadStatus::kUnreadable;
  }
  out.clear();
  out.reserve(json.Bans.size());
  for (const auto &text : json.Bans) {
    if (!idTextValid(text))
      continue;
    const std::uint64_t id = idFromText(text);
    if (std::find(out.begin(), out.end(), id) != out.end())
      continue;
    out.push_back(id);
    if (out.size() >= kMaxBans)
      break;
  }
  std::sort(out.begin(), out.end());
  return LoadStatus::kLoaded;
}

bool VoiceBanStore::save(const std::vector<std::uint64_t> &bans) {
  VoiceBanJson json;
  json.Bans.reserve(bans.size());
  for (const std::uint64_t id : bans)
    json.Bans.push_back(textFromId(id));
  if (!abc::JsonSave(json, kStorePath)) {
    abc::ConsoleWarn("[AbcUi] could not persist the voice ban store\n");
    return false;
  }
  return true;
}

} // namespace voice
} // namespace services
