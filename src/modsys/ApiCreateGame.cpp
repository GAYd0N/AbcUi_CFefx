// framework::CreateGameApi - the sdk creategame section (the create-local-
// game page's form data and the validated submit). See ApiTable.h for how
// it joins the module table.
#include "ApiTable.h"
#include "ApiSnapshot.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "../services/creategame/CreateGameModel.h"
#include "../services/creategame/CreateGameService.h"

#include <abc/CvarCmd.h>

namespace framework {
namespace CreateGameApi {
namespace {

void copyText(const std::string &text, char *out, uint32_t outSize) {
  if (!out || outSize == 0)
    return;
  uint32_t i = 0;
  for (; i + 1 < outSize && i < text.size(); ++i)
    out[i] = text[i];
  out[i] = '\0';
}

services::creategame::CreateGameModel *model() {
  return services::creategame::access().model;
}

const char *apiDefaultName(void) {
  const char *live = abc::CVAR_GET_STRING("hostname");
  return live ? live : "";
}

uint32_t apiMapCount(void) {
  const auto *target = model();
  return target ? static_cast<uint32_t>(target->catalog().size()) : 0;
}

const char *apiMapAt(int32_t index) {
  const auto *target = model();
  if (!target || index < 0 ||
      static_cast<size_t>(index) >= target->catalog().size())
    return "";
  return target->catalog().nameAt(static_cast<size_t>(index)).c_str();
}

int32_t apiOptionCount(void) {
  const auto *target = model();
  return target ? static_cast<int32_t>(target->options().size()) : 0;
}

int32_t apiOptionAt(int32_t index, AbcUiCgOption *out) {
  const auto *target = model();
  if (!target || !out || index < 0 ||
      static_cast<size_t>(index) >= target->options().size())
    return 0;
  const services::creategame::ServerOption &option =
      target->options()[static_cast<size_t>(index)];

  AbcUiCgOption full{};
  full.structSize = sizeof(AbcUiCgOption);
  copyText(option.Name, full.name, sizeof(full.name));
  copyText(option.Label, full.label, sizeof(full.label));
  full.toggle = option.Toggle ? 1 : 0;
  full.hasChoices = !option.Choices.empty() ? 1 : 0;
  full.slider = option.Bounded && option.Min < option.Max ? 1 : 0;
  full.min = option.Min;
  full.max = option.Max;
  full.defaultValue = option.Default;
  full.choiceCount = static_cast<int32_t>(option.Choices.size());

  // The caller's declared size wins: never write past an older header.
  abi::copySnapshot(out, full);
  return 1;
}

int32_t apiChoiceAt(int32_t option, int32_t index, AbcUiCgChoice *out) {
  const auto *target = model();
  if (!target || !out || option < 0 || index < 0 ||
      static_cast<size_t>(option) >= target->options().size())
    return 0;
  const services::creategame::ServerOption &definition =
      target->options()[static_cast<size_t>(option)];
  if (static_cast<size_t>(index) >= definition.Choices.size())
    return 0;
  const services::creategame::ServerOptionChoice &choice =
      definition.Choices[static_cast<size_t>(index)];

  AbcUiCgChoice full{};
  full.structSize = sizeof(AbcUiCgChoice);
  full.value = choice.Value;
  copyText(choice.Label, full.label, sizeof(full.label));

  abi::copySnapshot(out, full);
  return 1;
}

int32_t apiSubmit(const AbcUiCgField *fields, int32_t fieldCount, char *hint,
                  uint32_t hintSize) {
  const services::creategame::Access &seam = services::creategame::access();
  if (!fields || fieldCount < 0 || !seam.submit)
    return 0;
  std::vector<std::pair<std::string, std::string>> parsed;
  parsed.reserve(static_cast<size_t>(fieldCount));
  for (int32_t i = 0; i < fieldCount; ++i)
    parsed.emplace_back(fields[i].name, fields[i].value);
  std::string hintToken;
  const bool ok = seam.submit(parsed, hintToken);
  if (!ok && hint && hintSize > 0)
    copyText(hintToken, hint, hintSize);
  return ok ? 1 : 0;
}

const AbcUiCreateGameApi kCreateGameApi = {
    sizeof(AbcUiCreateGameApi),
    apiDefaultName,
    apiMapCount,
    apiMapAt,
    apiOptionCount,
    apiOptionAt,
    apiChoiceAt,
    apiSubmit,
};

} // namespace

const AbcUiCreateGameApi *get() {
  return &kCreateGameApi;
}

} // namespace CreateGameApi
} // namespace framework
