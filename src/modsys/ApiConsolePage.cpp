// framework::ConsolePageApi - the sdk consolepage section (the replacement
// console's data and services). See ApiTable.h for how it joins the module
// table.
#include "ApiTable.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "../framework/Localization.h"
#include "../services/console/ConsoleBackend.h"
#include "../services/console/ConsoleBuffer.h"
#include "../services/console/ConsolePageService.h"

namespace framework {
namespace ConsolePageApi {
namespace {

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

uint64_t apiGeneration(void) {
  return buffer().generation();
}

uint64_t apiLatestSequence(void) {
  return buffer().latestSequence();
}

int32_t apiLineCount(void) {
  return static_cast<int32_t>(buffer().lineCount());
}

int32_t apiCopySince(uint64_t since, AbcUiConsoleLine *out, uint32_t max) {
  if (!out || max == 0)
    return 0;
  std::vector<services::console::ConsoleLine> lines;
  buffer().copySince(since, lines, max);
  const uint32_t count =
      std::min<uint32_t>(max, static_cast<uint32_t>(lines.size()));
  for (uint32_t i = 0; i < count; ++i) {
    AbcUiConsoleLine full{};
    full.structSize = sizeof(AbcUiConsoleLine);
    full.developer = lines[i].Developer ? 1 : 0;
    full.truncated = lines[i].Truncated ? 1 : 0;
    copyText(lines[i].Text, full.text, sizeof(full.text));
    const uint32_t declared = out[i].structSize == 0
                                  ? static_cast<uint32_t>(sizeof(full))
                                  : out[i].structSize;
    std::memcpy(&out[i], &full, std::min<size_t>(declared, sizeof(full)));
  }
  return static_cast<int32_t>(count);
}

void apiPushLocalLine(const char *text) {
  if (!text)
    return;
  buffer().pushText(text, std::strlen(text), false);
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
  buffer().pushText(line.data(), line.size(), false);
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

int32_t apiComplete(const char *prefix, AbcUiConsoleSuggestion *out,
                    uint32_t max) {
  if (!prefix || !*prefix)
    return 0;
  std::vector<std::string> matches = backend().complete(prefix, max);
  const uint32_t count =
      out && max > 0
          ? std::min<uint32_t>(max, static_cast<uint32_t>(matches.size()))
          : 0;
  for (uint32_t i = 0; i < count; ++i) {
    AbcUiConsoleSuggestion full{};
    full.structSize = sizeof(AbcUiConsoleSuggestion);
    copyText(matches[i], full.name, sizeof(full.name));
    const uint32_t declared = out[i].structSize == 0
                                  ? static_cast<uint32_t>(sizeof(full))
                                  : out[i].structSize;
    std::memcpy(&out[i], &full, std::min<size_t>(declared, sizeof(full)));
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
