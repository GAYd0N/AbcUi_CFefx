// framework::VoteMenuApi - the sdk votemenu section (the vote-creation page
// model). See ApiTable.h for how it joins the module table.
#include "ApiTable.h"
#include "ApiSnapshot.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "../services/votemenu/VoteMenuModel.h"
#include "../services/votemenu/VoteMenuService.h"

namespace framework {
namespace VoteMenuApi {
namespace {

void copyText(const std::string &text, char *out, uint32_t outSize) {
  if (!out || outSize == 0)
    return;
  uint32_t i = 0;
  for (; i + 1 < outSize && i < text.size(); ++i)
    out[i] = text[i];
  out[i] = '\0';
}

services::votemenu::VoteMenuModel *model() {
  return services::votemenu::model();
}

void apiPrepare(void) {
  if (auto *target = model())
    target->prepare();
}

void apiRefresh(void) {
  if (auto *target = model())
    target->refresh();
}

uint32_t apiRevision(void) {
  const auto *target = model();
  return target ? static_cast<uint32_t>(target->revision()) : 0;
}

int32_t apiKind(void) {
  const auto *target = model();
  return target ? static_cast<int32_t>(target->kind()) : ABCUI_VOTE_KIND_BAN;
}

void apiSetKind(int32_t kind) {
  auto *target = model();
  if (!target)
    return;
  if (kind < ABCUI_VOTE_KIND_BAN || kind > ABCUI_VOTE_KIND_MAP)
    return;
  target->setKind(static_cast<services::votemenu::Kind>(kind));
}

void apiSetFilter(const char *filter) {
  if (auto *target = model())
    target->setFilter(filter ? filter : "");
}

int32_t apiVisibleCount(void) {
  const auto *target = model();
  return target ? static_cast<int32_t>(target->visible().size()) : 0;
}

int32_t apiVisibleAt(int32_t index, AbcUiVoteCandidate *out) {
  const auto *target = model();
  if (!target || !out || index < 0)
    return 0;
  const auto &visible = target->visible();
  if (static_cast<size_t>(index) >= visible.size())
    return 0;
  const services::votemenu::Candidate &candidate =
      visible[static_cast<size_t>(index)];
  const services::votemenu::Candidate *selected = target->selected();

  AbcUiVoteCandidate full{};
  full.structSize = sizeof(AbcUiVoteCandidate);
  full.kind = static_cast<int32_t>(candidate.kind);
  full.slot = candidate.slot;
  full.selected = selected && selected->slot == candidate.slot &&
                          selected->value == candidate.value
                      ? 1
                      : 0;
  copyText(candidate.name, full.name, sizeof(full.name));
  copyText(candidate.realName, full.realName, sizeof(full.realName));
  copyText(candidate.steamId, full.steamId, sizeof(full.steamId));

  // The caller's declared size wins: never write past an older header.
  abi::copySnapshot(out, full);
  return 1;
}

int32_t apiSelectedIndex(void) {
  const auto *target = model();
  if (!target)
    return -1;
  const services::votemenu::Candidate *selected = target->selected();
  if (!selected)
    return -1;
  const auto &visible = target->visible();
  for (size_t i = 0; i < visible.size(); ++i) {
    if (&visible[i] == selected)
      return static_cast<int32_t>(i);
  }
  return -1;
}

void apiSelect(int32_t index) {
  auto *target = model();
  if (!target || index < 0)
    return;
  target->select(static_cast<std::size_t>(index));
}

int32_t apiSubmit(char *command, uint32_t commandSize, char *error,
                  uint32_t errorSize) {
  auto *target = model();
  if (!target)
    return 0;
  std::string text;
  const char *errorToken = nullptr;
  if (!target->submitCommand(text, errorToken)) {
    if (error && errorSize > 0)
      copyText(errorToken ? std::string(errorToken) : std::string(), error,
               errorSize);
    return 0;
  }
  if (command && commandSize > 0)
    copyText(text, command, commandSize);
  return 1;
}

const AbcUiVoteMenuApi kVoteMenuApi = {
    sizeof(AbcUiVoteMenuApi),
    apiPrepare,
    apiRefresh,
    apiRevision,
    apiKind,
    apiSetKind,
    apiSetFilter,
    apiVisibleCount,
    apiVisibleAt,
    apiSelectedIndex,
    apiSelect,
    apiSubmit,
};

} // namespace

const AbcUiVoteMenuApi *get() {
  return &kVoteMenuApi;
}

} // namespace VoteMenuApi
} // namespace framework
