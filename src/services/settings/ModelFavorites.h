#pragma once

// services::settings::ModelFavorites - the player-model favorite list
// (issue 16: legacy ABCEnchance favmodels parity, new store).
//
// The model_favorites section in abc/abcui/config.json is persisted through
// the shared user-config owner and atomic sibling replacement. Entries are
// model directory names under models/player - the same tokens the `model` cvar
// stores.
//
// JSON keys and semantics - NEVER rename (persisted contract):
//   favorites: array of model names (empty array = no favorites).
//
// Mutations persist immediately through the shared owner.
// A failed write keeps the in-memory state and the caller surfaces the
// outcome; the next mutation retries the write.

#include <string>
#include <vector>

namespace services {
namespace settings {

class ModelFavorites {
public:
  enum class LoadStatus {
    kLoaded,     // store read; favorites adopted
    kMissing,    // no file yet (first run); empty list, saves allowed
    kUnreadable, // present but undecodable; empty list, saves refused
  };

  LoadStatus load();
  // True when the store came up clean and mutations may persist.
  bool savesAllowed() const {
    return SavesAllowed_;
  }
  bool isFavorite(const std::string &model) const;
  // Updates the in-memory list, then persists. Returns false when the
  // write failed (the in-memory state still reflects the request).
  bool setFavorite(const std::string &model, bool favorite);
  const std::vector<std::string> &favorites() const {
    return Favorites_;
  }

private:
  bool save() const;
  // Drops empty/control/separator entries, dedupes and sorts; guards the
  // in-memory set against a hand-edited or damaged store.
  static void sanitize(std::vector<std::string> &favorites);

  std::vector<std::string> Favorites_; // sorted, unique
  bool SavesAllowed_ = false;
};

} // namespace settings
} // namespace services
