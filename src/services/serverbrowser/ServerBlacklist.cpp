#include "ServerBlacklist.h"
#include <algorithm>
#include <charconv>
#include <ctime>

namespace services::serverbrowser {
uint64_t ServerBlacklist::nowSeconds() {
  const std::time_t Now = std::time(nullptr);
  return Now > 0 ? static_cast<uint64_t>(Now) : 0;
}
std::string BlacklistRule::address() const {
  std::string Text =
      std::to_string(Ip >> 24) + "." + std::to_string((Ip >> 16) & 255) + "." +
      std::to_string((Ip >> 8) & 255) + "." + std::to_string(Ip & 255);
  if (Port)
    Text += ":" + std::to_string(Port);
  return Text;
}
bool ServerBlacklist::parseAddress(const std::string &Text,
                                   BlacklistRule &Output) {
  if (Text.empty() || Text.size() > 21)
    return false;
  BlacklistRule Result;
  size_t Start = 0;
  for (int I = 0; I < 4; ++I) {
    const size_t End = Text.find(I < 3 ? '.' : ':', Start);
    const size_t Limit = End == std::string::npos ? Text.size() : End;
    unsigned Number = 0;
    if (Limit == Start || Limit - Start > 3)
      return false;
    const auto R =
        std::from_chars(Text.data() + Start, Text.data() + Limit, Number);
    if (R.ec != std::errc() || R.ptr != Text.data() + Limit || Number > 255)
      return false;
    Result.Ip = (Result.Ip << 8) | Number;
    if (I < 3 && End == std::string::npos)
      return false;
    Start = Limit + 1;
  }
  if (!Result.Ip)
    return false;
  if (Start <= Text.size()) {
    unsigned Port = 0;
    const auto R =
        std::from_chars(Text.data() + Start, Text.data() + Text.size(), Port);
    if (R.ec != std::errc() || R.ptr != Text.data() + Text.size() || !Port ||
        Port > 65535)
      return false;
    Result.Port = static_cast<uint16_t>(Port);
  }
  Output = std::move(Result);
  return true;
}
bool ServerBlacklist::contains(uint32_t Ip, uint16_t ConnectionPort) const {
  return std::any_of(Rules.begin(), Rules.end(), [&](const auto &Rule) {
    return Rule.Ip == Ip && (!Rule.Port || Rule.Port == ConnectionPort);
  });
}
bool ServerBlacklist::commit(std::vector<BlacklistRule> Next) {
  if (Save && !Save(Next))
    return false;
  Rules = std::move(Next);
  ++Revision;
  return true;
}
bool ServerBlacklist::add(const std::string &Address, const std::string &Name) {
  BlacklistRule Rule;
  if (!parseAddress(Address, Rule) || Rules.size() >= 1024 || Name.size() > 512)
    return false;
  if (std::any_of(Rules.begin(), Rules.end(), [&](const auto &Existing) {
        return Existing.Ip == Rule.Ip && Existing.Port == Rule.Port;
      }))
    return true;
  Rule.Name = Name;
  Rule.BlockedAt = Clock ? Clock() : 0;
  auto Next = Rules;
  Next.push_back(std::move(Rule));
  return commit(std::move(Next));
}
bool ServerBlacklist::remove(const std::string &Address) {
  BlacklistRule Rule;
  if (!parseAddress(Address, Rule))
    return false;
  auto Next = Rules;
  const auto removed = std::erase_if(Next, [&](const auto &Existing) {
    return Existing.Ip == Rule.Ip && Existing.Port == Rule.Port;
  });
  return removed == 0 || commit(std::move(Next));
}
} // namespace services::serverbrowser
