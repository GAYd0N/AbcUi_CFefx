#pragma once
#include <string>
#include <vector>

namespace services::creategame {
struct ServerOptionChoice {
  float Value = 0;
  std::string Label;
};
struct ServerOption {
  std::string Name;
  std::string Label;
  float Default = 0;
  float Min = 0;
  float Max = 1;
  bool Toggle = false;
  bool Bounded = true;
  std::vector<ServerOptionChoice> Choices;
};
struct ServerOptionValue {
  std::string Name;
  std::string Value;
};
// settings.scr is a data file, not executable commands. Only numeric mp_/sv_
// options are admitted; hostname/password/LAN/maxplayers have typed adapters.
std::vector<ServerOption> parseServerOptions(const std::string &Text);
bool validateServerOptions(const std::vector<ServerOption> &Definitions,
                           const std::vector<ServerOptionValue> &Values);
} // namespace services::creategame
