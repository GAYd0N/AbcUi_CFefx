#include "ItemList.h"

#include <algorithm>
#include <charconv>
#include <system_error>
#include <utility>

namespace itemlist {
namespace {

std::vector<std::string> tokenize(const std::string &content) {
  std::vector<std::string> tokens;
  std::size_t at = 0;
  while (at < content.size()) {
    while (at < content.size() && (content[at] == ' ' || content[at] == '\t' ||
                                   content[at] == '\r' || content[at] == '\n'))
      at++;
    const std::size_t begin = at;
    while (at < content.size() && content[at] != ' ' && content[at] != '\t' &&
           content[at] != '\r' && content[at] != '\n')
      at++;
    if (at > begin)
      tokens.push_back(content.substr(begin, at - begin));
  }
  return tokens;
}

int parseChannel(const std::string &text) {
  int value = 0;
  const char *const end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, value);
  if (result.ec != std::errc{} || result.ptr != end)
    return 0;
  return value;
}

} // namespace

bool parse(const std::string &content, std::vector<Entry> &out) {
  const std::vector<std::string> tokens = tokenize(content);
  const std::size_t records = tokens.size() / 6;
  for (std::size_t record = 0; record < records; ++record) {
    Entry entry;
    entry.path = tokens[record * 6];
    entry.nameToken = tokens[record * 6 + 1];
    for (int channel = 0; channel < 4; ++channel)
      entry.color[channel] = static_cast<unsigned char>(
          std::clamp(parseChannel(tokens[record * 6 + 2 + channel]), 0, 255));
    out.push_back(std::move(entry));
  }
  return tokens.size() % 6 == 0;
}

} // namespace itemlist
