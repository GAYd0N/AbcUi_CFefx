#include "SayText.h"

#include <cstring>
#include <utility>

namespace chatsaytext {
namespace {

void appendRun(std::vector<Run> &runs, RunKind kind, const std::string &text) {
  if (text.empty())
    return;
  if (!runs.empty() && runs.back().kind == kind) {
    runs.back().text += text;
    return;
  }
  runs.push_back(Run{text, kind});
}

std::string substituteParams(const std::string &format,
                             const char *const params[4]) {
  bool any = false;
  for (int i = 0; i < 4; i++) {
    if (params[i] && params[i][0])
      any = true;
  }
  if (!any)
    return format;
  std::string out;
  out.reserve(format.size());
  int next = 0;
  for (size_t i = 0; i < format.size(); i++) {
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
        const int position = format[i + 2] - '1';
        if (position < 4) {
          out += params[position] ? params[position] : "";
          i += 2;
          continue;
        }
        out += c;
        continue;
      }
      if (next < 4)
        out += params[next] ? params[next++] : (next++, "");
      i++;
      continue;
    }
    out += c;
  }
  return out;
}

// Splits a leading "(TEAM) " / "*DEAD* " decoration off the first text run.
void splitPrefix(std::vector<Run> &runs) {
  static const char *const kPrefixes[] = {"(TEAM) ", "*DEAD* ", "*SPEC* ",
                                          "*SPECTATOR* "};
  if (runs.empty() || runs.front().kind != RunKind::kText)
    return;
  for (const char *prefix : kPrefixes) {
    const size_t length = std::strlen(prefix);
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

std::vector<Run> buildRuns(const std::string &text, const char *const params[4],
                           const std::string &senderName) {
  std::string body = substituteParams(text, params);
  while (!body.empty() && (body.back() == '\n' || body.back() == '\r'))
    body.pop_back();

  std::vector<Run> runs;
  RunKind kind = RunKind::kText;
  std::string pending;
  for (size_t i = 0; i < body.size();) {
    const unsigned char byte = static_cast<unsigned char>(body[i]);
    if (byte == 0x01 || byte == 0x03 || byte == 0x04) {
      appendRun(runs, kind, pending);
      pending.clear();
      kind = byte == 0x01   ? RunKind::kText
             : byte == 0x03 ? RunKind::kName
                            : RunKind::kLocation;
      i++;
      continue;
    }
    if (byte == 0x02) {
      // Legacy marker: the sender name sits inside the text.
      appendRun(runs, kind, pending);
      pending.clear();
      i++;
      const size_t at =
          senderName.empty() ? std::string::npos : body.find(senderName, i);
      if (at == std::string::npos)
        continue;
      appendRun(runs, RunKind::kText, body.substr(i, at - i));
      appendRun(runs, RunKind::kName, senderName);
      i = at + senderName.size();
      continue;
    }
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

} // namespace chatsaytext
