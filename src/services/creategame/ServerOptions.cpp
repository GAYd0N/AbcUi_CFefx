#include "ServerOptions.h"

#include <abc/Parse.h>
#include "services/settings/SettingsModel.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

namespace services::creategame {
std::vector<ServerOption> parseServerOptions(const std::string &Text) {
  settings::SettingsModel::UserScrTable Table;
  settings::SettingsModel::parseUserScr(Text.c_str(), Table);
  std::vector<ServerOption> Result;
  std::set<std::string> Names;
  for (const auto &Row : Table.Rows) {
    const std::string Name = Row.Cvar;
    if (Name == "sv_lan" || Name == "sv_password" || Name == "hostname" ||
        Name == "maxplayers")
      continue;
    if ((Name.rfind("mp_", 0) != 0 && Name.rfind("sv_", 0) != 0) ||
        !std::all_of(Name.begin(), Name.end(),
                     [](unsigned char C) {
                       return (C >= 'a' && C <= 'z') ||
                              (C >= '0' && C <= '9') || C == '_';
                     }) ||
        !Names.insert(Name).second)
      continue;
    if (Row.Kind != settings::SettingKind::kBool &&
        Row.Kind != settings::SettingKind::kFloat &&
        Row.Kind != settings::SettingKind::kEnum)
      continue;
    ServerOption Option{
        Name,       Row.LabelToken, Row.DefaultValue,
        Row.Min,    Row.Max,        Row.Kind == settings::SettingKind::kBool,
        Row.Bounded};
    for (int I = 0; I < Row.OptionCount; ++I)
      Option.Choices.push_back({Row.Options[I].Value, Row.Options[I].Label});
    if (!validateServerOptions(
            {Option}, {{Name, Row.DefaultString ? Row.DefaultString : ""}}))
      continue;
    Result.push_back(std::move(Option));
  }
  return Result;
}

bool validateServerOptions(const std::vector<ServerOption> &Definitions,
                           const std::vector<ServerOptionValue> &Values) {
  if (Values.size() != Definitions.size())
    return false;
  std::set<std::string> Seen;
  for (const auto &Entry : Values) {
    auto It = std::find_if(
        Definitions.begin(), Definitions.end(),
        [&](const auto &Definition) { return Definition.Name == Entry.Name; });
    if (It == Definitions.end() || !Seen.insert(Entry.Name).second ||
        Entry.Value.empty() || Entry.Value.size() > 32)
      return false;
    // Entire text must be a finite number, not a command delimiter.
    const auto Parsed = abc::ParseFloat(Entry.Value);
    if (!Parsed || !std::isfinite(*Parsed))
      return false;
    const float Number = *Parsed;
    if (It->Toggle && Number != 0 && Number != 1)
      return false;
    if (!It->Choices.empty()) {
      if (std::none_of(
              It->Choices.begin(), It->Choices.end(),
              [&](const auto &Choice) { return Choice.Value == Number; }))
        return false;
    } else if (It->Bounded && (Number < It->Min || Number > It->Max)) {
      return false;
    }
  }
  return true;
}
} // namespace services::creategame
