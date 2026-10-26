// framework::MenuConfigApi - the sdk menuconfig section (the menu shell's
// definition and its engine facts). See ApiTable.h for how it joins the
// module table.
#include "ApiTable.h"
#include "ModuleVfs.h"
#include "ModuleResources.h"
#include "ApiSnapshot.h"

#include <algorithm>
#include <cstring>

#include "../services/uiconfig/MenuShellService.h"
#include "../services/uiconfig/UiDefinitions.h"

namespace framework {
namespace MenuConfigApi {
namespace {

void copyText(const std::string &text, char *out, uint32_t outSize) {
  if (!out || outSize == 0)
    return;
  uint32_t i = 0;
  for (; i + 1 < outSize && i < text.size(); ++i)
    out[i] = text[i];
  out[i] = '\0';
}

const services::uiconfig::MenuShellAccess &access() {
  return services::uiconfig::shellAccess();
}

const std::vector<services::uiconfig::MenuEntry> *entries(int32_t section) {
  const services::uiconfig::MenuDefinition *definition = access().definition;
  if (!definition)
    return nullptr;
  switch (section) {
  case ABCUI_MENU_SECTION_MAIN:
    return &definition->Main;
  case ABCUI_MENU_SECTION_PAUSE:
    return &definition->Pause;
  default:
    return nullptr;
  }
}

const char *apiTitle(void) {
  const services::uiconfig::MenuDefinition *definition = access().definition;
  return definition ? definition->Title.c_str() : "";
}

int32_t apiEntryCount(int32_t section) {
  const auto *list = entries(section);
  return list ? static_cast<int32_t>(list->size()) : 0;
}

int32_t apiEntryAt(int32_t section, int32_t index, AbcUiMenuConfigEntry *out) {
  const auto *list = entries(section);
  if (!list || !out || index < 0 || static_cast<size_t>(index) >= list->size())
    return 0;
  const services::uiconfig::MenuEntry &entry =
      (*list)[static_cast<size_t>(index)];

  AbcUiMenuConfigEntry full{};
  full.structSize = sizeof(AbcUiMenuConfigEntry);
  copyText(entry.Id, full.id, sizeof(full.id));
  copyText(entry.Label, full.label, sizeof(full.label));
  copyText(entry.Action, full.action, sizeof(full.action));
  copyText(entry.Condition, full.condition, sizeof(full.condition));

  // The caller's declared size wins: never write past an older header.
  abi::copySnapshot(out, full);
  return 1;
}

int32_t apiMultiplayer(void) {
  const auto &seam = access();
  return seam.multiplayer && seam.multiplayer() ? 1 : 0;
}

int32_t apiPlayerCard(AbcUiPlayerCard *out) {
  const auto &seam = access();
  if (!seam.playerCard || !out)
    return 0;
  std::string playerName, modelPath, modelLabel, actionModel, weaponModel;
  int sequence = 0;
  seam.playerCard(playerName, modelPath, modelLabel, actionModel, weaponModel,
                  sequence);

  AbcUiPlayerCard full{};
  full.structSize = sizeof(AbcUiPlayerCard);
  copyText(playerName, full.playerName, sizeof(full.playerName));
  copyText(modelPath, full.modelPath, sizeof(full.modelPath));
  copyText(modelLabel, full.modelLabel, sizeof(full.modelLabel));
  copyText(actionModel, full.actionModel, sizeof(full.actionModel));
  copyText(weaponModel, full.weaponModel, sizeof(full.weaponModel));
  full.sequence = sequence;

  abi::copySnapshot(out, full);
  return 1;
}

int32_t apiBuildNote(char *out, uint32_t outSize) {
  const auto &seam = access();
  if (!seam.buildNote)
    return 0;
  std::string note;
  seam.buildNote(note);
  copyText(note, out, outSize);
  return static_cast<int32_t>(note.size());
}

int32_t apiSetResourceRoot(const char *uri) {
  const auto owner = module_resources::current();
  if (!uri || !owner || !owner->initializing || !module_resources::accepting())
    return ABCUI_ERROR_FORBIDDEN;
  std::string root;
  if (!ModuleVfs::resolve({}, std::string(uri) + "/_", root) ||
      !root.starts_with("mod://" + owner->id + "/"))
    return ABCUI_ERROR_INVALID;
  root.resize(root.size() - 2);
  static module_resources::Registration lifetime;
  if (lifetime.tracked())
    return ABCUI_ERROR_FORBIDDEN;
  lifetime = module_resources::Registration{};
  services::uiconfig::setResourceRoot(std::move(root));
  lifetime.track([] { services::uiconfig::setResourceRoot({}); });
  return ABCUI_OK;
}

const AbcUiMenuConfigApi kMenuConfigApi = {
    sizeof(AbcUiMenuConfigApi),
    apiSetResourceRoot,
    apiTitle,
    apiEntryCount,
    apiEntryAt,
    apiMultiplayer,
    apiPlayerCard,
    apiBuildNote,
};

} // namespace

const AbcUiMenuConfigApi *get() {
  return &kMenuConfigApi;
}

} // namespace MenuConfigApi
} // namespace framework
