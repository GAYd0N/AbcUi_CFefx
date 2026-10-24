// framework::Localization implementation - see Localization.h.
#include "Localization.h"
#include "TextUtil.h"

#include <abc/Console.h>

#include "Language.h"
#include "../modsys/ModuleVfs.h"
#include "../modsys/ModuleResources.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <span>
#include <map>
#include <unordered_map>
#include <vector>
#include <string_view>

namespace framework {

namespace {

using TranslationTable = std::unordered_map<std::string, std::string>;
std::map<std::string, TranslationTable> &moduleTables() {
  static std::map<std::string, TranslationTable> instance;
  return instance;
}

// "key = value" lines; '#' starts a comment. Values keep interior spaces.
void parseTable(std::span<const std::byte> bytes, TranslationTable &table,
                const std::string &prefix = {}) {
  std::string line;
  auto parseLine = [&]() {
    std::string key, value;
    if (TextUtil::parseTranslationLine(line, key, value) &&
        (prefix.empty() || key.starts_with(prefix)))
      table[key] = value;
    line.clear();
  };
  for (const std::byte byte : bytes) {
    const unsigned char ch = std::to_integer<unsigned char>(byte);
    if (ch == '\n')
      parseLine();
    else if (line.size() < 65536)
      line.push_back(static_cast<char>(ch));
  }
  parseLine();
}

void parseModuleTable(const std::string &id, const std::string &language) {
  PackageEntry entry;
  const std::string path = "lang/" + language + ".txt";
  if (ModuleVfs::openPath(id.c_str(), path.c_str(), entry, nullptr) &&
      entry.size <= 1024 * 1024)
    parseTable({static_cast<const std::byte *>(entry.data), entry.size},
               moduleTables()[id], id + "/");
}
} // namespace

void Localization::load() {
  moduleTables().clear();

  const char *language = CurrentLanguage();
  if (!language || !*language)
    language = "english";

  for (const auto &id : ModuleVfs::mountedIds()) {
    parseModuleTable(id, "english");
    if (std::string_view(language) != "english")
      parseModuleTable(id, language);
  }
}

bool Localization::translate(std::string &out, const std::string &token) {
  std::string qualified = token;
  if (qualified.find('/') == std::string::npos) {
    const auto owner = module_resources::current();
    if (!owner)
      return false;
    qualified = owner->id + "/" + token;
  }
  const auto slash = qualified.find('/');
  const auto id = qualified.substr(0, slash);
  const auto owner = module_resources::current();
  if (owner && !ModuleVfs::canRead(owner->id, "mod://" + id + "/_"))
    return false;
  if (!ModuleVfs::isMounted(id.c_str()))
    return false;
  const auto table = moduleTables().find(id);
  if (table == moduleTables().end())
    return false;
  const auto entry = table->second.find(qualified);
  if (entry == table->second.end())
    return false;
  out = entry->second;
  return true;
}

} // namespace framework
