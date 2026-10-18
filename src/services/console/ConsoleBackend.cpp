// services::console::ConsoleBackend implementation - see ConsoleBackend.h.
#include "ConsoleBackend.h"

#include <algorithm>

namespace services::console {

namespace {

bool isSpace(char c) {
  return c == ' ' || c == '\t';
}

std::string trimCopy(const char *text) {
  std::string trimmed(text);
  std::size_t begin = 0;
  while (begin < trimmed.size() && isSpace(trimmed[begin]))
    ++begin;
  std::size_t end = trimmed.size();
  while (end > begin && isSpace(trimmed[end - 1]))
    --end;
  return trimmed.substr(begin, end - begin);
}

char lowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool prefixMatches(const std::string &prefix, const std::string &name) {
  if (prefix.size() > name.size())
    return false;
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (lowerAscii(prefix[i]) != lowerAscii(name[i]))
      return false;
  }
  return true;
}

bool namesEqual(const std::string &left, const std::string &right) {
  if (left.size() != right.size())
    return false;
  for (std::size_t i = 0; i < left.size(); ++i) {
    if (lowerAscii(left[i]) != lowerAscii(right[i]))
      return false;
  }
  return true;
}

// The first verb of a console line, following the engine's own tokenizer:
// a quoted verb is one token, and ';' chains / whitespace end it. Anything
// with structure this cannot classify ($ substitution, a comment) is left to
// the caller as "not classifiable" through an empty result.
bool firstVerb(const char *line, std::string &out) {
  const std::string text(line ? line : "");
  std::size_t i = 0;
  while (i < text.size() && isSpace(text[i]))
    ++i;
  if (i < text.size() && text[i] == '"') {
    ++i;
    while (i < text.size() && text[i] != '"')
      out += text[i++];
    return true;
  }
  if (i < text.size() && (text[i] == '$' || text[i] == '/' || text[i] == ';'))
    return false; // substitution / comment / empty chain: not classifiable
  while (i < text.size() && !isSpace(text[i]) && text[i] != ';')
    out += text[i++];
  return true;
}

} // namespace

bool ConsoleBackend::sensitiveCommand(const std::string &command) {
  // Conservatively omit sensitive identifiers anywhere in chained commands,
  // quoted alias bodies or arguments. This is a history policy, not a
  // restriction on execution; no partial command parser can leak a later
  // password command by inspecting only the first verb.
  std::string Folded = command;
  std::transform(Folded.begin(), Folded.end(), Folded.begin(), lowerAscii);
  return Folded.find("password") != std::string::npos;
}

ConsoleBackend::SubmitResult ConsoleBackend::submit(const char *text) {
  if (!text || !*text) {
    ++Stats.RejectedEmpty;
    return SubmitResult::kRejectedEmpty;
  }
  const std::string command = trimCopy(text);
  if (command.empty()) {
    ++Stats.RejectedEmpty;
    return SubmitResult::kRejectedEmpty;
  }
  if (command.size() > kMaxCommandBytes ||
      command.find('\n') != std::string::npos ||
      command.find('\r') != std::string::npos) {
    ++Stats.RejectedInvalid;
    return SubmitResult::kRejectedInvalid;
  }
  if (!Seams_.clientCmd) {
    ++Stats.Unavailable;
    return SubmitResult::kUnavailable;
  }
  std::string line = command;
  line += '\n'; // exactly one command line, exactly one terminator
  if (!Seams_.clientCmd(line.c_str())) {
    ++Stats.Unavailable;
    return SubmitResult::kUnavailable;
  }
  ++Stats.Executed;
  if (sensitiveCommand(command)) {
    ++Stats.HistorySkippedSensitive;
  } else {
    if (History.size() >= kMaxHistory)
      History.pop_front();
    History.push_back(command);
  }
  // The engine took the line either way; the classification only tells the
  // caller whether the client runtime catalog recognizes the first verb.
  return knownVerb(command.c_str()) ? SubmitResult::kExecuted
                                    : SubmitResult::kExecutedUnknownVerb;
}

void ConsoleBackend::collectNames(std::vector<std::string> &names) const {
  const char *borrowed[kMaxEnumeration];
  const auto collect = [&](std::size_t (*list)(const char **, std::size_t)) {
    if (!list)
      return;
    const std::size_t count = list(borrowed, kMaxEnumeration);
    for (std::size_t i = 0; i < count && i < kMaxEnumeration; ++i) {
      if (borrowed[i] && *borrowed[i])
        names.emplace_back(borrowed[i]); // copy now; never keep the pointer
    }
  };
  collect(Seams_.listCvars);
  collect(Seams_.listCommands);
}

std::vector<std::string> ConsoleBackend::complete(const char *prefix,
                                                  std::size_t max) const {
  std::vector<std::string> matches;
  if (max == 0)
    return matches;
  const std::size_t cap = max < kMaxCompletions ? max : kMaxCompletions;
  const std::string needle = prefix ? prefix : "";

  std::vector<std::string> names;
  collectNames(names);

  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  for (const std::string &name : names) {
    if (!prefixMatches(needle, name))
      continue;
    matches.push_back(name);
    if (matches.size() >= cap)
      break;
  }
  return matches;
}

bool ConsoleBackend::knownVerb(const char *line) const {
  // Fail-open: an incomplete catalog cannot answer "unknown" honestly (a
  // name may simply live in the half that is not wired).
  if (!Seams_.listCvars || !Seams_.listCommands)
    return true;
  const std::string verb = verbOf(line);
  if (verb.empty())
    return true;
  std::vector<std::string> names;
  collectNames(names);
  for (const std::string &name : names) {
    if (namesEqual(name, verb))
      return true;
  }
  return false;
}

std::string ConsoleBackend::verbOf(const char *line) {
  std::string verb;
  if (!firstVerb(line, verb))
    return std::string();
  return verb;
}

ConsoleBackend &consoleBackend() {
  static ConsoleBackend Backend;
  return Backend;
}

} // namespace services::console
