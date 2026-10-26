// framework::ConsolePageApi - the sdk consolepage section (the replacement
// console's data and services). See ApiTable.h for how it joins the module
// table.
#include "ApiTable.h"
#include "ApiSnapshot.h"

#include <algorithm>
#include <string>
#include <vector>

#include "../framework/Localization.h"
#include "../services/console/ConsoleBackend.h"
#include "../services/console/ConsoleBuffer.h"
#include "../services/console/ConsolePageService.h"

namespace framework {
namespace ConsolePageApi {
namespace {

static_assert(sizeof(AbcUiConsoleLine) == 1036);
static_assert(sizeof(AbcUiConsoleSuggestion) == 132);

// The ESC layering claim (see noteEscapeClaim): the console page consumed
// an ESC press on one of its own layers; the menu owner's routed ESC
// reads-and-clears it so one physical press never closes two layers.
bool &escapeClaimSlot() {
  static bool claimed = false;
  return claimed;
}

void copyText(const std::string &text, char *out, uint32_t outSize) {
  if (!out || outSize == 0)
    return;
  uint32_t i = 0;
  for (; i + 1 < outSize && i < text.size(); ++i)
    out[i] = text[i];
  out[i] = '\0';
}

services::console::ConsoleBuffer &buffer() {
  return services::console::consoleBuffer();
}

services::console::ConsoleBackend &backend() {
  return services::console::consoleBackend();
}

// One complete local line into the mirror. The buffer assembles fragments
// until a newline, so a local line without its terminator would stay
// pending and join whatever the engine prints next on the same row.
void pushLocalLine(const std::string &text) {
  if (text.empty())
    return;
  std::string line = text;
  if (line.back() != '\n')
    line.push_back('\n');
  buffer().pushText(line.data(), line.size(), false);
}

uint64_t apiGeneration(void) {
  return buffer().generation();
}

uint64_t apiLatestSequence(void) {
  return buffer().latestSequence();
}

int32_t apiLineCount(void) {
  return static_cast<int32_t>(buffer().lineCount());
}

void fillLine(const services::console::ConsoleLine &line, void *out) {
  AbcUiConsoleLine full{};
  full.structSize = sizeof(full);
  full.developer = line.Developer ? 1 : 0;
  full.truncated = line.Truncated ? 1 : 0;
  copyText(line.Text, full.text, sizeof(full.text));
  abi::copySnapshot(out, full);
}

int32_t apiCopySince(uint64_t since, void *out, uint32_t max, uint32_t stride) {
  if (!out || max == 0)
    return 0;
  if (!abi::validArrayLayout(max, stride))
    return -1;
  std::vector<services::console::ConsoleLine> lines;
  buffer().copySince(since, lines, max);
  const uint32_t count =
      std::min<uint32_t>(max, static_cast<uint32_t>(lines.size()));
  if (!abi::validSnapshotArray(out, count, stride))
    return -1;
  for (uint32_t i = 0; i < count; ++i)
    fillLine(lines[i], abi::arrayElement(out, i, stride));
  return static_cast<int32_t>(count);
}

void apiPushLocalLine(const char *text) {
  if (!text)
    return;
  pushLocalLine(text);
}

int32_t apiSubmit(const char *command, int32_t *unknown) {
  if (!command || !*command)
    return 0;
  const auto result = backend().submit(command);
  switch (result) {
  case services::console::ConsoleBackend::SubmitResult::kExecuted:
    if (unknown)
      *unknown = 0;
    return 1;
  case services::console::ConsoleBackend::SubmitResult::kExecutedUnknownVerb:
    if (unknown)
      *unknown = 1;
    return 1;
  case services::console::ConsoleBackend::SubmitResult::kRejectedEmpty:
  case services::console::ConsoleBackend::SubmitResult::kRejectedInvalid:
  case services::console::ConsoleBackend::SubmitResult::kUnavailable:
    return 0;
  }
  return 0;
}

void apiNoteUnknownCommand(const char *command) {
  if (!command || !*command)
    return;
  const std::string verb = services::console::ConsoleBackend::verbOf(command);
  if (verb.empty())
    return; // not classifiable: never guess a name
  std::string line;
  if (!framework::Localization::translate(line, "Console_UnknownCommand"))
    line = "Unknown command: %s";
  const std::string marker = "%s";
  const auto at = line.find(marker);
  if (at != std::string::npos)
    line.replace(at, marker.size(), verb);
  pushLocalLine(line);
}

int32_t apiHistoryCount(void) {
  return static_cast<int32_t>(backend().history().size());
}

int32_t apiHistoryAt(int32_t index, char *out, uint32_t outSize) {
  const auto &history = backend().history();
  if (index < 0 || static_cast<size_t>(index) >= history.size())
    return 0;
  copyText(history[static_cast<size_t>(index)], out, outSize);
  return 1;
}

void fillSuggestion(const std::string &name, void *out) {
  AbcUiConsoleSuggestion full{};
  full.structSize = sizeof(full);
  copyText(name, full.name, sizeof(full.name));
  abi::copySnapshot(out, full);
}

int32_t apiComplete(const char *prefix, void *out, uint32_t max,
                    uint32_t stride) {
  if (!prefix || !*prefix)
    return 0;
  if (out && max != 0 && !abi::validArrayLayout(max, stride))
    return -1;
  const auto matches = backend().complete(prefix, max);
  if (out && max != 0) {
    const uint32_t count =
        std::min<uint32_t>(max, static_cast<uint32_t>(matches.size()));
    if (!abi::validSnapshotArray(out, count, stride))
      return -1;
    for (uint32_t i = 0; i < count; ++i)
      fillSuggestion(matches[i], abi::arrayElement(out, i, stride));
  }
  return static_cast<int32_t>(matches.size());
}

void apiRequestClose(void) {
  // The page's close button follows the engine console hotkey's funnel: the
  // bridge truth lowers, and the engine-side `hideconsole` runs so the
  // engine's console flag cannot outlive the close (issue #5). The funnel
  // is the composition root's seam - both edges live there.
  const services::console::PageAccess &access = services::console::pageAccess();
  if (access.requestClose)
    access.requestClose();
}

void apiNoteEscapeClaim(void) {
  escapeClaimSlot() = true;
}

const AbcUiConsolePageApi kConsolePageApi = {
    sizeof(AbcUiConsolePageApi),
    apiGeneration,
    apiLatestSequence,
    apiLineCount,
    apiCopySince,
    apiPushLocalLine,
    apiSubmit,
    apiNoteUnknownCommand,
    apiHistoryCount,
    apiHistoryAt,
    apiComplete,
    apiRequestClose,
    apiNoteEscapeClaim,
};

} // namespace

const AbcUiConsolePageApi *get() {
  return &kConsolePageApi;
}

// framework-internal (the menu owner's routed ESC): reads-and-clears the
// console page's one-layer claim.
bool consumeEscapeClaim() {
  bool &claimed = escapeClaimSlot();
  const bool value = claimed;
  claimed = false;
  return value;
}

} // namespace ConsolePageApi
} // namespace framework
