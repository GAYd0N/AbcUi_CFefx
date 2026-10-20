// services::settings::ModelFavorites implementation - see the header.
#include "ModelFavorites.h"

#include <AIGCJson.hpp>

#include <abc/Config.h>
#include <abc/Console.h>

#include <algorithm>
#include <cstring>

namespace services {
namespace settings {

namespace {
constexpr const char *kStorePath = "abc/abcui/model_favorites.json";
// The scan feeding the picker caps at the same count; a list beyond this is
// treated as a damaged store rather than an achievement.
constexpr size_t kMaxFavorites = 512;

// The on-disk schema. Field order defines the written key order only; the
// parser is key-based (a missing key keeps the default).
struct ModelFavoritesJson {
  std::vector<std::string> Favorites;
  AIGC_JSON_HELPER(Favorites)
  AIGC_JSON_HELPER_RENAME("favorites")
};

bool entryTextValid(const std::string &model) {
  if (model.empty() || model.size() > 63)
    return false;
  for (const char c : model) {
    const unsigned char byte = static_cast<unsigned char>(c);
    if (byte < 0x20 || byte == 0x7f)
      return false;
    if (std::strchr("\"'`\\;/|<>*?[]:", c))
      return false;
  }
  return model != "." && model != "..";
}
} // namespace

ModelFavorites::LoadStatus ModelFavorites::load() {
  ModelFavoritesJson json;
  const auto status = abc::JsonLoadWithStatus(json, kStorePath);
  if (status == abc::JsonLoadStatus::Missing) {
    Favorites_.clear();
    SavesAllowed_ = true;
    return LoadStatus::kMissing;
  }
  if (status != abc::JsonLoadStatus::Success) {
    // Unreadable store: fail closed into an empty list and refuse to save
    // (never overwrite a damaged user file - the blacklist store's rule).
    abc::ConsoleWarn("[AbcUi] model favorites unreadable (%d)\n",
                     static_cast<int>(status));
    Favorites_.clear();
    SavesAllowed_ = false;
    return LoadStatus::kUnreadable;
  }
  Favorites_ = std::move(json.Favorites);
  sanitize(Favorites_);
  SavesAllowed_ = true;
  return LoadStatus::kLoaded;
}

bool ModelFavorites::isFavorite(const std::string &model) const {
  return std::binary_search(Favorites_.begin(), Favorites_.end(), model);
}

bool ModelFavorites::setFavorite(const std::string &model, bool favorite) {
  if (!entryTextValid(model))
    return false;
  const auto itor =
      std::lower_bound(Favorites_.begin(), Favorites_.end(), model);
  const bool present = itor != Favorites_.end() && *itor == model;
  if (favorite == present)
    return true; // no state change; the store already matches
  if (favorite)
    Favorites_.insert(itor, model);
  else
    Favorites_.erase(itor);
  return save();
}

bool ModelFavorites::save() const {
  ModelFavoritesJson json;
  json.Favorites = Favorites_;
  if (!abc::JsonSave(json, kStorePath)) {
    abc::ConsoleWarn("[AbcUi] could not persist the model favorites\n");
    return false;
  }
  return true;
}

void ModelFavorites::sanitize(std::vector<std::string> &favorites) {
  std::vector<std::string> clean;
  clean.reserve(favorites.size());
  for (auto &model : favorites) {
    if (!entryTextValid(model))
      continue;
    if (std::find(clean.begin(), clean.end(), model) != clean.end())
      continue;
    clean.push_back(std::move(model));
    if (clean.size() >= kMaxFavorites)
      break;
  }
  std::sort(clean.begin(), clean.end());
  favorites = std::move(clean);
}

} // namespace settings
} // namespace services
