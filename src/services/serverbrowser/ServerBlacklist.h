#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace services::serverbrowser {
struct BlacklistRule {
  uint32_t Ip = 0;
  uint16_t Port = 0; // zero means all connection ports on this IPv4 address
  std::string Name;
  // Unix time (seconds) the rule was added; 0 = unknown (a rule loaded
  // from a file written before the field existed). Never fabricated.
  uint64_t BlockedAt = 0;
  std::string address() const;
};

class ServerBlacklist {
public:
  using SaveSink = std::function<bool(const std::vector<BlacklistRule> &)>;
  // The wall clock used to stamp new rules. Mirrors ServerQuery::setClock:
  // the pure service never reads the engine/system time itself, and with
  // no clock wired the stamp stays 0 (honest "unknown", never a guess).
  using ClockFn = uint64_t (*)();
  static uint64_t nowSeconds();
  static bool parseAddress(const std::string &Text, BlacklistRule &Output);
  void setSaveSink(SaveSink Sink) {
    Save = std::move(Sink);
  }
  void setClock(ClockFn clock) {
    Clock = clock;
  }
  void restore(std::vector<BlacklistRule> Value) {
    Rules = std::move(Value);
    ++Revision;
  }
  bool add(const std::string &Address, const std::string &Name = {});
  bool remove(const std::string &Address);
  bool contains(uint32_t Ip, uint16_t ConnectionPort) const;
  const std::vector<BlacklistRule> &rules() const {
    return Rules;
  }
  uint64_t revision() const {
    return Revision;
  }

private:
  bool commit(std::vector<BlacklistRule> Next);
  std::vector<BlacklistRule> Rules;
  SaveSink Save;
  ClockFn Clock = nullptr;
  uint64_t Revision = 0;
};
} // namespace services::serverbrowser
