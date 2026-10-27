// services::settings::ModelFavorites implementation - see the header.
#include "ModelFavorites.h"
#include "ChoiceValidation.h"
#include "../UserConfig.h"

#include <AIGCJson.hpp>

#include <abc/Console.h>

#include <algorithm>

namespace services {
namespace settings {

namespace {
constexpr const char *kSection = "model_favorites";

// The on-disk schema. Field order defines the written key order only; the
// parser is key-based (a missing key keeps the default).
struct ModelFavoritesJson {
  std::vector<std::string> Favorites;
  AIGC_JSON_HELPER(Favorites)
  AIGC_JSON_HELPER_RENAME("favorites")
};

bool entryTextValid(const std::string &model) {
  return model.size() <= 63 && choiceTokenValid(model);
}
} // namespace

ModelFavorites::LoadStatus ModelFavorites::load() {
  ModelFavoritesJson json;
  const auto status = userconfig::load(json, kSection);
  if (status == userconfig::LoadStatus::Missing) {
    Favorites_.clear();
    SavesAllowed_ = true;
    return LoadStatus::kMissing;
  }
  if (status != userconfig::LoadStatus::Loaded) {
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
  if (!SavesAllowed_ || !entryTextValid(model))
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
  if (!userconfig::save(json, kSection)) {
    abc::ConsoleWarn("[AbcUi] could not persist the model favorites\n");
    return false;
  }
  return true;
}

void ModelFavorites::sanitize(std::vector<std::string> &favorites) {
  std::erase_if(favorites,
                [](const auto &model) { return !entryTextValid(model); });
  std::ranges::sort(favorites);
  const auto duplicates = std::ranges::unique(favorites);
  favorites.erase(duplicates.begin(), duplicates.end());
}

} // namespace settings
} // namespace services
