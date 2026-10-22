// framework::MenuConfigApi - the sdk menuconfig section (the menu shell's
// definition and its engine facts). See ApiTable.h for how it joins the
// module table.
#include "ApiTable.h"

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
  const uint32_t declared = out->structSize == 0
                                ? static_cast<uint32_t>(sizeof(full))
                                : out->structSize;
  std::memcpy(out, &full, std::min<size_t>(declared, sizeof(full)));
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
  std::string playerName, modelPath, modelLabel;
  seam.playerCard(playerName, modelPath, modelLabel);

  AbcUiPlayerCard full{};
  full.structSize = sizeof(AbcUiPlayerCard);
  copyText(playerName, full.playerName, sizeof(full.playerName));
  copyText(modelPath, full.modelPath, sizeof(full.modelPath));
  copyText(modelLabel, full.modelLabel, sizeof(full.modelLabel));

  const uint32_t declared = out->structSize == 0
                                ? static_cast<uint32_t>(sizeof(full))
                                : out->structSize;
  std::memcpy(out, &full, std::min<size_t>(declared, sizeof(full)));
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

const AbcUiMenuConfigApi kMenuConfigApi = {
    sizeof(AbcUiMenuConfigApi),
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
