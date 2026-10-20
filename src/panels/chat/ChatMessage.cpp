// panels::chat::message implementation - see ChatMessage.h.
#include "ChatMessage.h"

#include <cstring>

namespace panels {
namespace chat {

namespace {

// SayText colour control bytes (HLSDK hud_saytext.cpp TEXTCOLOR_*).
constexpr char kColorNormal = 0x01;
constexpr char kColorOldColors = 0x02;
constexpr char kColorPlayerName = 0x03;
constexpr char kColorLocation = 0x04;

// The client's own decorations on a composed line; the panel tints the run.
constexpr const char *kPrefixes[] = {"(TEAM) ", "*DEAD* ", "*SPEC* ",
                                     "*SPECTATOR* "};

bool hasParams(const std::string (&params)[4]) {
  for (const auto &param : params) {
    if (!param.empty())
      return true;
  }
  return false;
}

// Positional substitution for the HLSDK SayText shape: `%s1`..`%s9` pick a
// parameter by position, a bare `%s` takes the next one, `%%` is a literal
// percent. Everything else stays verbatim, so a server line that merely
// contains '%' is never interpreted as a format string (the D-17 rule the
// TextMsg path follows as well).
std::string substituteParams(const std::string &format,
                             const std::string (&params)[4]) {
  if (!hasParams(params))
    return format;
  std::string out;
  out.reserve(format.size());
  int next = 0;
  for (std::size_t i = 0; i < format.size(); i++) {
    const char c = format[i];
    if (c != '%' || i + 1 >= format.size()) {
      out += c;
      continue;
    }
    const char spec = format[i + 1];
    if (spec == '%') {
      out += '%';
      i++;
      continue;
    }
    if (spec == 's') {
      if (i + 2 < format.size() && format[i + 2] >= '1' &&
          format[i + 2] <= '9') {
        out += params[format[i + 2] - '1'];
        i += 2;
        continue;
      }
      if (next < 4)
        out += params[next++];
      i++;
      continue;
    }
    out += c;
  }
  return out;
}

void appendRun(std::vector<Run> &runs, RunKind kind, const std::string &text) {
  if (text.empty())
    return;
  if (!runs.empty() && runs.back().kind == kind) {
    runs.back().text += text;
    return;
  }
  runs.push_back(Run{text, kind});
}

// Splits a leading "(TEAM) " / "*DEAD* " decoration off the first text run.
void splitPrefix(std::vector<Run> &runs) {
  if (runs.empty() || runs.front().kind != RunKind::kText)
    return;
  for (const char *prefix : kPrefixes) {
    const std::size_t length = std::strlen(prefix);
    if (runs.front().text.compare(0, length, prefix) != 0)
      continue;
    Run head{prefix, RunKind::kPrefix};
    runs.front().text.erase(0, length);
    if (runs.front().text.empty())
      runs.erase(runs.begin());
    runs.insert(runs.begin(), std::move(head));
    return;
  }
}

} // namespace

std::vector<Run> buildRuns(const std::string &text,
                           const std::string (&params)[4],
                           const std::string &senderName) {
  std::string body = substituteParams(text, params);
  while (!body.empty() && (body.back() == '\n' || body.back() == '\r'))
    body.pop_back();

  std::vector<Run> runs;
  RunKind kind = RunKind::kText;
  std::string pending;
  for (std::size_t i = 0; i < body.size();) {
    const unsigned char byte = static_cast<unsigned char>(body[i]);
    if (byte == kColorNormal || byte == kColorPlayerName ||
        byte == kColorLocation) {
      appendRun(runs, kind, pending);
      pending.clear();
      kind = byte == kColorNormal       ? RunKind::kText
             : byte == kColorPlayerName ? RunKind::kName
                                        : RunKind::kLocation;
      i++;
      continue;
    }
    if (byte == kColorOldColors) {
      // Legacy marker: the sender name sits inside the text, so the run has
      // to be found by content rather than announced by a marker.
      appendRun(runs, kind, pending);
      pending.clear();
      kind = RunKind::kText;
      i++;
      const std::size_t at =
          senderName.empty() ? std::string::npos : body.find(senderName, i);
      if (at == std::string::npos)
        continue;
      appendRun(runs, RunKind::kText, body.substr(i, at - i));
      appendRun(runs, RunKind::kName, senderName);
      i = at + senderName.size();
      continue;
    }
    // No other control character has a display meaning; drop it instead of
    // letting it reach the DOM.
    if (byte < 0x20 || byte == 0x7f) {
      i++;
      continue;
    }
    pending += static_cast<char>(byte);
    i++;
  }
  appendRun(runs, kind, pending);
  splitPrefix(runs);
  return runs;
}

} // namespace chat
} // namespace panels
