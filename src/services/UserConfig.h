#pragma once

#include <AIGCJson.hpp>

#include <string>
#include <utility>

namespace services::userconfig {

enum class LoadStatus { Loaded, Missing, Unreadable };

// One process-lifetime owner merges section updates into config.json.
// Unknown sections survive writes. A damaged file is never overwritten.
LoadStatus readSection(const char *name, std::string &json);
bool writeSection(const char *name, const std::string &json);
bool renderBackend(std::string &id);

template <class T> LoadStatus load(T &value, const char *section) {
  std::string json;
  const auto status = readSection(section, json);
  if (status != LoadStatus::Loaded)
    return status;
  T candidate = value;
  if (!aigc::JsonHelper::JsonToObject(candidate, json))
    return LoadStatus::Unreadable;
  value = std::move(candidate);
  return LoadStatus::Loaded;
}

template <class T> bool save(T &value, const char *section) {
  T previous{};
  if (load(previous, section) == LoadStatus::Unreadable)
    return false;
  std::string json;
  return aigc::JsonHelper::ObjectToJson(value, json) &&
         writeSection(section, json);
}

} // namespace services::userconfig
