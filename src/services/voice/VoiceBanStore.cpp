// services::voice::VoiceBanStore implementation - see the header.
#include "VoiceBanStore.h"
#include "../UserConfig.h"

#include <AIGCJson.hpp>

#include <abc/Console.h>
#include <abc/Parse.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace services {
namespace voice {

namespace {
constexpr const char *kSection = "voice_bans";
bool &savesAllowed() {
  static bool allowed = false;
  return allowed;
}
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
std::optional<std::uint64_t> idFromText(std::string_view text) {
  if (text.empty() || text.size() > 20)
    return std::nullopt;
  if (text.size() > 1 && text[0] == '0')
    return std::nullopt;
  if (!std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; }))
    return std::nullopt;
  const auto value = abc::ParseNumber<std::uint64_t>(text);
  return value && *value != 0 ? value : std::nullopt;
}

std::string textFromId(std::uint64_t id) {
  return std::format("{}", id);
}
} // namespace

VoiceBanStore::LoadStatus VoiceBanStore::load(std::vector<std::uint64_t> &out) {
  VoiceBanJson json;
  const auto status = userconfig::load(json, kSection);
  savesAllowed() = status != userconfig::LoadStatus::Unreadable;
  if (status == userconfig::LoadStatus::Missing) {
    out.clear();
    return LoadStatus::kMissing;
  }
  if (status != userconfig::LoadStatus::Loaded) {
    abc::ConsoleWarn("[AbcUi] voice ban store unreadable (%d)\n",
                     static_cast<int>(status));
    out.clear();
    return LoadStatus::kUnreadable;
  }
  out.clear();
  out.reserve(json.Bans.size());
  for (const auto &text : json.Bans) {
    const auto id = idFromText(text);
    if (!id)
      continue;
    if (std::ranges::find(out, *id) != out.end())
      continue;
    out.push_back(*id);
    if (out.size() >= kMaxBans)
      break;
  }
  std::sort(out.begin(), out.end());
  return LoadStatus::kLoaded;
}

bool VoiceBanStore::save(const std::vector<std::uint64_t> &bans) {
  if (!savesAllowed())
    return false;
  VoiceBanJson json;
  json.Bans.reserve(bans.size());
  for (const std::uint64_t id : bans)
    json.Bans.push_back(textFromId(id));
  if (!userconfig::save(json, kSection)) {
    abc::ConsoleWarn("[AbcUi] could not persist the voice ban store\n");
    return false;
  }
  return true;
}

} // namespace voice
} // namespace services
