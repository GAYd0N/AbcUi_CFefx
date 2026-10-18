// services::creategame implementation - see CreateGameModel.h for the frozen
// evidence, the recoverable/irreversible classification and the seams.
#include "CreateGameModel.h"

#include <abc/Parse.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <format>

namespace services {
namespace creategame {

namespace {

char lowerAscii(char c) {
  if (c >= 'A' && c <= 'Z')
    return static_cast<char>(c - 'A' + 'a');
  return c;
}

bool equalsIgnoreCase(const std::string &a, const std::string &b) {
  if (a.size() != b.size())
    return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (lowerAscii(a[i]) != lowerAscii(b[i]))
      return false;
  }
  return true;
}

// Strict base-10 integer parse: the whole string, digits only, no sign, no
// whitespace, no overflow (the 9-digit cap keeps 11+ digit garbage away).
bool parseStrictDigits(const std::string &text, long &out) {
  if (text.empty() || text.size() > 9)
    return false;
  if (text.find_first_not_of("0123456789") != std::string::npos)
    return false;
  const auto value = abc::ParseInt(text);
  if (!value)
    return false;
  out = *value;
  return true;
}

} // namespace

// --- MapCatalog ------------------------------------------------------------

bool MapCatalog::validMapToken(const char *map) {
  if (!map || !*map)
    return false;
  const std::size_t length = std::strlen(map);
  if (length > 127)
    return false;
  const auto isFirst = [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' ||
           c == '_';
  };
  const auto isRest = [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' ||
           c == '_' || c == '.' || c == '/';
  };
  if (!isFirst(map[0]))
    return false;
  for (std::size_t i = 1; i < length; ++i) {
    if (!isRest(map[i]))
      return false;
  }
  if (std::strstr(map, "..") != nullptr)
    return false;
  if (map[length - 1] == '/' || map[length - 1] == '.')
    return false;
  for (std::size_t i = 0; i + 1 < length; ++i) {
    if (map[i] == '/' && map[i + 1] == '/')
      return false;
  }
  return true;
}

bool MapCatalog::addRawName(const char *relativeName) {
  if (!relativeName || !*relativeName)
    return false;
  if (Names.size() >= kCapacity)
    return false;
  std::string name = relativeName;
  for (char &c : name) {
    if (c == '\\')
      c = '/';
  }
  // The shipped strip is strstr-based ("maps/" + 5); keep the LAST
  // occurrence so a nested engine path still yields the map token.
  std::size_t prefix = std::string::npos;
  for (std::size_t i = name.size(); i >= 5; --i) {
    const std::size_t at = i - 5;
    if (lowerAscii(name[at]) == 'm' && lowerAscii(name[at + 1]) == 'a' &&
        lowerAscii(name[at + 2]) == 'p' && lowerAscii(name[at + 3]) == 's' &&
        name[at + 4] == '/') {
      prefix = at;
      break;
    }
  }
  if (prefix != std::string::npos)
    name = name.substr(prefix + 5);
  if (name.size() > 4 && equalsIgnoreCase(name.substr(name.size() - 4), ".bsp"))
    name = name.substr(0, name.size() - 4);
  if (!validMapToken(name.c_str()))
    return false;
  Names.push_back(std::move(name));
  return true;
}

void MapCatalog::finalize() {
  std::sort(Names.begin(), Names.end(),
            [](const std::string &a, const std::string &b) {
              const std::size_t count =
                  a.size() < b.size() ? a.size() : b.size();
              for (std::size_t i = 0; i < count; ++i) {
                const char la = lowerAscii(a[i]);
                const char lb = lowerAscii(b[i]);
                if (la != lb)
                  return la < lb;
              }
              return a.size() < b.size();
            });
  Names.erase(std::unique(Names.begin(), Names.end(),
                          [](const std::string &a, const std::string &b) {
                            return equalsIgnoreCase(a, b);
                          }),
              Names.end());
}

void MapCatalog::clear() {
  Names.clear();
}

bool MapCatalog::contains(const std::string &map) const {
  if (!validMapToken(map.c_str()))
    return false;
  for (const std::string &name : Names) {
    if (equalsIgnoreCase(name, map))
      return true;
  }
  return false;
}

// --- validation ------------------------------------------------------------

bool CreateGameModel::validTextChar(char c) {
  const unsigned char byte = static_cast<unsigned char>(c);
  if (byte < 0x20 || byte == 0x7F)
    return false; // control bytes (newline/tab included)
  if (byte >= 0x80)
    return true; // UTF-8 continuation/lead bytes: ordinary text
  return c != '"' && c != ';' && c != '\\';
}

bool CreateGameModel::parsePlayers(const char *text, int &out) {
  if (!text)
    return false;
  long value = 0;
  if (!parseStrictDigits(text, value))
    return false;
  if (value < kMinPlayers || value > kMaxPlayers)
    return false;
  out = static_cast<int>(value);
  return true;
}

Validation CreateGameModel::validate(const MapCatalog &catalog,
                                     const Request &request) {
  Validation result;
  // Map: grammar first (traversal/charset/length), then the enumerated-set
  // membership - never a path, never an unknown name.
  result.field = Field::kMap;
  if (request.Map.empty()) {
    result.error = Error::kMapEmpty;
    return result;
  }
  if (!MapCatalog::validMapToken(request.Map.c_str())) {
    result.error = Error::kMapToken;
    return result;
  }
  if (!catalog.contains(request.Map)) {
    result.error = Error::kMapNotEnumerated;
    return result;
  }
  // Player count: the settings.scr row limit [1, 32] (parsePlayers is the
  // text-level gate; this is the value-level defense in depth).
  result.field = Field::kMaxPlayers;
  if (request.MaxPlayers < kMinPlayers || request.MaxPlayers > kMaxPlayers) {
    result.error = Error::kPlayersRange;
    return result;
  }
  // Server name: required, 63-byte cap, no command/infostring delimiters.
  result.field = Field::kServerName;
  if (request.ServerName.empty()) {
    result.error = Error::kNameEmpty;
    return result;
  }
  if (request.ServerName.size() > kMaxTextBytes) {
    result.error = Error::kNameLength;
    return result;
  }
  for (char c : request.ServerName) {
    if (!validTextChar(c)) {
      result.error = Error::kNameCharset;
      return result;
    }
  }
  // Password: optional; same byte cap and charset (space included in the
  // reject set: the client retry chain `password %s` takes one argument).
  result.field = Field::kPassword;
  if (request.Password.size() > kMaxTextBytes) {
    result.error = Error::kPasswordLength;
    return result;
  }
  for (char c : request.Password) {
    if (!validTextChar(c) || c == ' ') {
      result.error = Error::kPasswordCharset;
      return result;
    }
  }
  return result;
}

// --- launch ----------------------------------------------------------------

const char *CreateGameModel::recoverableName(int index) {
  switch (index) {
  case 0:
    return "sv_lan";
  case 1:
    return "sv_password";
  case 2:
    return "hostname";
  default:
    return "";
  }
}

bool CreateGameModel::writeAndVerify(int index, const char *value) {
  const char *name = recoverableName(index);
  if (!Seams_.writeCvarString(name, value))
    return false;
  Wrote[index] = true;
  const char *back = Seams_.readCvar(name);
  return back && value && std::strcmp(back, value) == 0;
}

CreateGameModel::Launch CreateGameModel::restoreAndFail(int failIndex) {
  FailIndex = failIndex;
  // Reverse order: restore every cvar this launch actually wrote (the
  // failing one included - its write returned true but did not stick).
  for (int i = failIndex >= kRecoverableCount ? kRecoverableCount - 1
                                              : failIndex;
       i >= 0; --i) {
    if (!Wrote[i] || !Snapshots[i].Present)
      continue;
    const char *name = recoverableName(i);
    bool ok = Seams_.writeCvarString(name, Snapshots[i].Value.c_str());
    if (ok) {
      const char *back = Seams_.readCvar(name);
      ok = back && Snapshots[i].Value == back;
    }
    if (!ok)
      ++RestoreFailures;
  }
  return Launch::kWriteFailed;
}

CreateGameModel::Launch CreateGameModel::launch(const Request &request) {
  FailIndex = -1;
  RestoreFailures = 0;
  for (int i = 0; i < kRecoverableCount; ++i) {
    Wrote[i] = false;
    Snapshots[i].Present = false;
    Snapshots[i].Value.clear();
  }
  if (!wired())
    return Launch::kUnwired;
  // Everything validated BEFORE any engine call (T19 step 3: 全部校验完成
  // 再写). A refused request produces zero seam invocations.
  if (!validate(Catalog, request).ok() || !validOptions(request))
    return Launch::kInvalid;

  // Snapshot the recoverable cvars BEFORE anything is written. A cvar that
  // is absent cannot be snapshotted; its write then fails verification and
  // the launch restores what came before it - fail closed.
  for (int i = 0; i < kRecoverableCount; ++i) {
    if (const char *live = Seams_.readCvar(recoverableName(i))) {
      Snapshots[i].Present = true;
      Snapshots[i].Value = live;
    }
  }

  // --- the shipped sequence (FUN_10003a10 / 0x1007f7a0), step by step ----
  // 1. disconnect: leave any current game (the shipped string opens with it).
  Seams_.clientCmd("disconnect\n");

  // 2. sv_lan 1 (recoverable).
  if (!writeAndVerify(0, request.Lan ? "1" : "0"))
    return restoreAndFail(0);

  // 3. setmaster enable (shipped constant).
  Seams_.clientCmd("setmaster enable\n");

  // 4. maxplayers %d: an engine command, not a cvar (see the header). The
  // value passed validate() [1, 32] - the settings.scr row limit - and the
  // engine's own handler clamps against the live player count.
  const std::string players =
      "maxplayers " + std::to_string(request.MaxPlayers) + "\n";
  Seams_.clientCmd(players.c_str());

  // 5. sv_password: the password never leaves this single typed write; it is
  // not logged, not snapshotted and not kept anywhere else.
  if (!writeAndVerify(1, request.Password.c_str()))
    return restoreAndFail(1);

  // 6. hostname.
  if (!writeAndVerify(2, request.ServerName.c_str()))
    return restoreAndFail(2);

  // 7. cd fadeout (shipped constant).
  Seams_.clientCmd("cd fadeout\n");

  // 8. the irreversible map change: last, and only with every write above
  // verified. Success is NOT declared here - the engine's loading stream
  // owns the outcome (T14 Loading, T19 step 5).
  const std::string command = "map " + request.Map + "\n";
  Seams_.clientCmd(command.c_str());

  // 9. the shipped trailer (a separate issue right after the template).
  Seams_.clientCmd("wait\nwait\n");
  // Server-DLL cvars may not exist before the first map. Apply numeric
  // settings after that DLL/map configuration has loaded. Both the names
  // and values passed the script allowlist and complete finite-number gate.
  for (const auto &Option : request.Options) {
    const auto Parsed = abc::ParseFloat(Option.Value);
    if (!Parsed)
      continue;
    const std::string Command =
        Option.Name + " " +
        std::format("{:.9g}", static_cast<double>(*Parsed)) + "\n";
    Seams_.clientCmd(Command.c_str());
  }
  return Launch::kLaunched;
}

std::vector<std::string> CreateGameModel::settleOptions(const Request &Value) {
  std::vector<std::string> Failed;
  if (!wired() || !validOptions(Value))
    return {"invalid request"};
  for (const auto &Option : Value.Options) {
    const auto Wanted = abc::ParseFloat(Option.Value);
    if (!Wanted)
      continue;
    auto Matches = [&]() {
      const auto *Live = Seams_.readCvar(Option.Name.c_str());
      if (!Live)
        return false;
      const auto Number = abc::ParseFloat(Live);
      return Number && std::abs(*Number - *Wanted) < 1e-5f;
    };
    if (Matches())
      continue;
    if (!Seams_.writeCvarString(Option.Name.c_str(), Option.Value.c_str()) ||
        !Matches())
      Failed.push_back(Option.Name);
  }
  return Failed;
}
} // namespace creategame
} // namespace services
