// The player-model picker: the filtered list, the favorites, the per-row
// context menu and its pointer handling.

#include "../shared/PageShared.h"
#include "SettingsInternal.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <string>

namespace settingspage {

// ---------------------------------------------------------- model picker --

// Byte-wise tolower: the filter compares model directory names, which the
// scan takes verbatim from the filesystem (ASCII names in practice; the
// legacy picker used the same ::tolower rule).
char lowerByte(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool containsCaseInsensitive(const std::string &value,
                             const std::string &filter) {
  if (filter.empty())
    return true;
  const auto at =
      std::search(value.begin(), value.end(), filter.begin(), filter.end(),
                  [](char a, char b) { return lowerByte(a) == lowerByte(b); });
  return at != value.end();
}

// The console's ancestry walk: a press belongs to the menu when the target is
// the frame itself or one of its items.
bool insideNode(AbcUiElement *element, AbcUiElement *ancestor) {
  for (AbcUiElement *it = element; it; it = Api->ui->elementParent(it)) {
    if (it == ancestor)
      return true;
  }
  return false;
}

AbcUiElement *actionNode(AbcUiElement *element) {
  for (AbcUiElement *it = element; it; it = Api->ui->elementParent(it)) {
    if (!attributeOf(it, "data-action").empty())
      return it;
  }
  return nullptr;
}

bool favoriteOn(const char *model) {
  return Api->settings && model && *model &&
         Api->settings->favoriteContains(model) != 0;
}

void closeModelContextMenu() {
  ModelContextOpen = false;
  ModelContextModel.clear();
  setDisplay("model-picker-context-menu", false);
}

void toggleFavorite(const std::string &model) {
  if (!Api->settings || model.empty())
    return;
  if (!Api->settings->favoritesAvailable())
    return; // read-only session: the status line explains why
  Api->settings->favoriteSet(model.c_str(), favoriteOn(model.c_str()) ? 0 : 1);
}

void renderModelPicker() {
  if (!Page || !Api->settings)
    return;
  AbcUiElement *list = node("model-picker-list");
  AbcUiElement *status = node("model-picker-status");
  AbcUiElement *favOnly = node("model-picker-favonly");
  if (!list || !status || !favOnly)
    return;

  AbcUiSettingRow model{};
  const bool haveModel =
      rowFound("model", model) && !(model.flags & ABCUI_SET_ROW_MISSING);
  const std::string current = haveModel ? model.draftText : std::string();
  const int32_t totalChoices = haveModel ? model.choiceCount : 0;

  int total = 0;
  int shown = 0;
  for (int32_t index = 0; index < totalChoices; ++index) {
    char name[ABCUI_SETTINGS_TEXT_MAX];
    name[0] = '\0';
    if (!Api->settings->choiceAt("model", index, name, sizeof(name), nullptr,
                                 0))
      continue;
    ++total;
    if (ModelFavOnly && !favoriteOn(name))
      continue;
    if (!containsCaseInsensitive(name, ModelFilter))
      continue;
    ++shown;
  }

  const std::string signature =
      ModelFilter + "|" + (ModelFavOnly ? "fav" : "all") + "|" + current;
  const uint32_t generation = Api->settings->favoritesGeneration();
  const bool rebuild = signature != ModelBuiltSignature ||
                       generation != ModelBuiltFavoriteGeneration;
  if (rebuild) {
    ModelBuiltSignature = signature;
    ModelBuiltFavoriteGeneration = generation;
    std::string markup;
    for (int32_t index = 0; index < totalChoices; ++index) {
      char name[ABCUI_SETTINGS_TEXT_MAX];
      name[0] = '\0';
      if (!Api->settings->choiceAt("model", index, name, sizeof(name), nullptr,
                                   0))
        continue;
      const std::string choice = name;
      if (ModelFavOnly && !favoriteOn(name))
        continue;
      if (!containsCaseInsensitive(choice, ModelFilter))
        continue;
      const bool favorite = favoriteOn(name);
      markup += "<button class=\"model-picker-row";
      if (choice == current)
        markup += " selected";
      markup += "\" data-action=\"" + std::string(kActionModelPick) +
                "\" data-choice=\"" + escapeAttribute(choice) +
                "\"><span class=\"model-picker-name\">" + escapeText(choice) +
                "</span><span class=\"model-picker-star";
      if (favorite)
        markup += " on";
      markup += "\" data-action=\"" + std::string(kActionModelFav) +
                "\" data-choice=\"" + escapeAttribute(choice) + "\">" +
                escapeText(resolve("#abcui.gameui/Settings_ModelStar")) +
                "</span></button>";
    }
    if (shown == 0) {
      markup += "<div class=\"model-picker-empty\">" +
                escapeText(resolve(
                    total == 0 ? "#abcui.gameui/Settings_ModelPickerEmpty"
                               : "#abcui.gameui/Settings_ModelPickerNoMatch")) +
                "</div>";
    }
    Api->ui->elementSetInnerRML(list, markup.c_str());
  }

  // The toggle's state and the count line are cheap writes: they follow every
  // render pass so external model/favorite changes stay honest.
  Api->ui->elementSetClass(favOnly, "on", ModelFavOnly);
  std::string statusText;
  if (!Api->settings->favoritesAvailable()) {
    statusText = resolve("#abcui.gameui/Settings_ModelFavLocked");
  } else if (total == 0) {
    statusText = resolve("#abcui.gameui/Settings_ModelPickerEmpty");
  } else {
    statusText = std::to_string(shown) + " / " + std::to_string(total) + " " +
                 resolve("#abcui.gameui/Settings_ModelPickerModels");
  }
  setInnerRml(status, escapeText(statusText));
}

bool handleModelAction(const std::string &action, AbcUiElement *entry) {
  if (action != kActionModelPick && action != kActionModelFav &&
      action != kActionModelFavOnly)
    return false;
  if (action == kActionModelFavOnly) {
    ModelFavOnly = !ModelFavOnly;
    return true;
  }
  const std::string name = attributeOf(entry, "data-choice");
  if (name.empty())
    return true; // picker intent without a payload: consumed, no-op
  if (action == kActionModelFav) {
    toggleFavorite(name);
    return true;
  }
  // The pick commits immediately: the draft feeds the shared <model> preview
  // on the next render, a single-row apply writes the cvar with the model's
  // own read-back; a refused write leaves the row dirty and the status bar
  // reports the pending change.
  if (Api->settings) {
    Api->settings->setDraftString("model", name.c_str());
    Api->settings->applyOne("model");
  }
  return true;
}

bool handleModelFilterChange(const std::string &value) {
  ModelFilter = value;
  renderModelPicker();
  return true;
}

bool handleModelDblClick(AbcUiElement *target) {
  // The double-click pair's first click already selected the row; the legacy
  // picker used the second press purely as a favorite toggle.
  AbcUiElement *entry = actionNode(target);
  if (!entry || attributeOf(entry, "data-action") != kActionModelPick)
    return false;
  const std::string name = attributeOf(entry, "data-choice");
  if (name.empty())
    return true;
  toggleFavorite(name);
  render();
  return true;
}

void openModelContextMenu(AbcUiElement *entry, float mouseX, float mouseY) {
  ModelContextModel = attributeOf(entry, "data-choice");
  if (ModelContextModel.empty())
    return;
  AbcUiElement *menu = node("model-picker-context-menu");
  if (!menu)
    return;
  // Viewport pixel coordinates (the same space as the mouse event), clamped
  // so a click near an edge still shows the whole entry. The size mirrors
  // model-picker-context-menu's fixed dp.
  float scale = 1.0f;
  if (!Api->ui->documentScale(Page, &scale) || scale <= 0.0f)
    scale = 1.0f;
  int width = 0;
  int height = 0;
  Api->ui->documentSize(Page, &width, &height);
  const float menuWidth = 104.0f * scale;
  const float menuHeight = 31.0f * scale;
  const float maxX = width > 0 ? std::max(0.0f, width - menuWidth) : 0.0f;
  const float maxY = height > 0 ? std::max(0.0f, height - menuHeight) : 0.0f;
  const std::string left =
      std::format("{:g}px", std::clamp(mouseX, 0.0f, maxX));
  const std::string top = std::format("{:g}px", std::clamp(mouseY, 0.0f, maxY));
  setProperty(menu, "left", left.c_str());
  setProperty(menu, "top", top.c_str());
  // The menu carries one fixed intent; its label flips with the target row's
  // current state.
  setInnerRml(node("model-picker-context-fav"),
              escapeText(resolve(favoriteOn(ModelContextModel.c_str())
                                     ? "#abcui.gameui/Settings_ModelFavRemove"
                                     : "#abcui.gameui/Settings_ModelFavAdd")));
  setDisplay(menu, true);
  ModelContextOpen = true;
}

bool handleModelPointerDown(AbcUiElement *target, int32_t button, float mouseX,
                            float mouseY) {
  AbcUiElement *menu = node("model-picker-context-menu");
  if (button == 1) {
    // Right press over a picker row opens the favorite menu at the pointer;
    // anywhere else an open menu just closes.
    AbcUiElement *entry = actionNode(target);
    if (entry && attributeOf(entry, "data-action") == kActionModelPick) {
      openModelContextMenu(entry, mouseX, mouseY);
      return false; // the press itself selects nothing
    }
    if (ModelContextOpen)
      closeModelContextMenu();
    return false;
  }
  // Any left press outside the menu dismisses it (the menu item's own click
  // path closes it after acting).
  if (ModelContextOpen && menu && !insideNode(target, menu))
    closeModelContextMenu();
  return false;
}

bool handleModelContextAction(const std::string &action) {
  if (action != kActionModelContextFav)
    return false;
  if (!ModelContextModel.empty())
    toggleFavorite(ModelContextModel);
  closeModelContextMenu();
  return true;
}

} // namespace settingspage
