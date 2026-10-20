#pragma once

// sections::ModelPicker - the I16 player-model picker widget (issue 16,
// legacy ABCEnchance parity): the models/player directory list with a
// case-insensitive substring filter, a favorites-only toggle and favorite
// marks.
//
// The list projects the `model` cvar's scanned options (SettingsModel)
// plus the persisted favorite set (ModelFavorites). Selecting a row
// commits the cvar immediately - draft + single-row apply, which also
// drives the shared <model> preview through the page's renderPreviews();
// a refused write keeps the row dirty and the page's own status bar
// reports it. Favorites mutate through the store, which persists every
// change itself. The plain `model` dropdown row is suppressed for the
// section that declares this component (sections/CvarRows).
//
// State is page-session-local: the filter text and the favorites-only
// toggle live as long as the settings document does.

#include <string>

namespace Rml {
class Element;
class ElementDocument;
class Event;
} // namespace Rml
namespace services {
namespace settings {
class SettingsModel;
class ModelFavorites;
} // namespace settings
} // namespace services

namespace panels::settings::sections {

class ModelPicker {
public:
  void setFavorites(services::settings::ModelFavorites *favorites) {
    Favorites = favorites;
  }

  // Projects the list + status from the model's scanned options, the
  // filter and the favorite set. Rows rebuild only when an input changed
  // (a rebuild drops hover/focus); the status line rewrites every pass.
  void render(Rml::ElementDocument *doc,
              services::settings::SettingsModel *model);

  // Delegated page events. Each returns true when the event belonged to
  // the picker (the page re-renders); false leaves the event to the rows.
  bool handleAction(const char *action, Rml::Element *entry,
                    services::settings::SettingsModel *model);
  bool handleFilterChange(Rml::Event &event, Rml::ElementDocument *doc,
                          services::settings::SettingsModel *model);
  // Double-click on a row is a favorite toggle (the legacy FavChange);
  // the first click of the pair has already selected the row.
  bool handleDblClick(Rml::Event &event);
  // Right-button down over a row opens the favorite context menu at the
  // pointer; any other press closes an open menu first.
  bool handlePointerDown(Rml::Event &event, Rml::ElementDocument *doc);
  // The context menu item's click: toggles the target row's favorite and
  // closes the menu.
  bool handleContextAction(const char *action, Rml::ElementDocument *doc);
  // True while the right-click menu is up (the page's ESC path closes it
  // before leaving, exactly like the spray browser layer).
  bool contextOpen() const {
    return ContextOpen;
  }
  void closeContextMenu(Rml::ElementDocument *doc);

private:
  void toggleFavorite(const std::string &model);

  services::settings::ModelFavorites *Favorites = nullptr;
  std::string Filter;
  bool FavOnly = false;
  std::string BuiltSignature;
  // Favorites generation the last build saw (bumped by toggleFavorite).
  unsigned FavGeneration = 0;
  unsigned BuiltFavGeneration = 0;
  // Right-click menu state: open flag + the row it targets (the menu
  // lives outside the scroll container, so it never scrolls away).
  bool ContextOpen = false;
  std::string ContextModel;
};

} // namespace panels::settings::sections
