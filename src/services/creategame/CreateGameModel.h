#pragma once

// services::creategame - the T19 local-server creation domain: the enumerated
// map set (MapCatalog), the typed request value object, its fail-closed
// validation, and the ordered launcher. Engine-free by design so the exact
// production logic links into the offline tests (05 doc tier 1); the module
// wires the seams and performs the engine filesystem enumeration.
//
// Engine evidence (Ghidra, GameUI.dll image base 0x10000000 - read-only):
//
//   * Map enumeration (CCreateMultiplayerGameServerPage::FillMapList
//     FUN_10005380 -> FUN_10005630): IFileSystem::FindFirst("maps/*.bsp",
//     &handle, pathID) / FindNext / FindClose over the engine's MOUNTED search
//     paths. The Sven Co-op branch passes pathID NULL and "GAMEDOWNLOAD"
//     (FUN_10005630 call sites; a non-Sven description additionally enumerates
//     "GAME"/"GAME_FALLBACK"). Each returned name is stripped of its "maps/"
//     prefix (strstr + 5) and its ".bsp" suffix (strstr) before insertion -
//     MapCatalog::addRawName keeps that shape and then applies the token
//     grammar. The dialog also injects a "< Random Map >" sentinel whose item
//     data resolves to a random other entry (FUN_10004e90); this model
//     deliberately offers the explicit enumerated maps only and records the
//     omission as a documented divergence.
//
//   * Launch sequence (CCreateMultiplayerGameDialog::Apply
//     FUN_10003a10, format string 0x1007f7a0): the exact shipped command text
//     is
//         disconnect\nsv_lan 1\nsetmaster enable\nmaxplayers %i\n
//         sv_password "%s"\nhostname "%s"\ncd fadeout\nmap %s\n
//     followed by a separate "wait\nwait\n" issue.  The three substitutions
//     are read from the gameplay page's settings.scr rows (control names =
//     cvar names; defaults: hostname = game description, sv_password = the
//     live cvar, maxplayers = atoi of the row).  The map token comes from the
//     MapList ComboBox selection (FUN_10004e90).
//
//   * Value semantics: `maxplayers` is an engine cvar whose change callback
//     refuses while a server is running ("maxplayers cannot be changed while
//     a server is running", hw.dll 0x01e71f84 string, callback LAB_01d79930
//     registered by FUN_01d773c0); `hostname`/`sv_lan`/`sv_password` are
//     registered at engine init (hw.dll FUN_01d65750 -> FUN_01d9f620).  The
//     shipped dialog's edit fields carry 64-byte strncpy buffers (0x40), so
//     request text is capped at 63 bytes.
//
//   * The settings.scr range row for maxplayers is NUMBER 1.000000 32.000000
//     (default 8) - the shipped UI's own limit; this model uses [1, 32].
//
// Classification the launch order derives from (T19 step 3):
//   RECOVERABLE cvars  - sv_lan, sv_password, hostname: written through the
//                        typed cvar API with a snapshot taken first and an
//                        immediate read-back per write. A refused or
//                        non-sticky write aborts BEFORE the map command and
//                        restores every already-written cvar from its snapshot
//                        (read back each restore).
//   ENGINE COMMAND     - `maxplayers %d`: NOT a cvar on this install (byte
//                        scan: the name exists in hw.dll only as the command
//                        registration 0x01d773c0 + the status query; there is
//                        no cvar entry - the value lives in the engine's
//                        maxclients global DAT_090e6f98, whose command handler
//                        refuses while a server runs and clamps to the current
//                        player count). The client has no handle to read its
//                        previous value, so it is emitted verbatim in its
//                        shipped position and its read-back belongs to the
//                        LIVE server (GetMaxClients after the join).
//   IRREVERSIBLE       - the `map` command (a map load cannot be undone); it
//                        is the LAST step and only ever runs after every
//                        recoverable write verified. The shipped constant
//                        actions (disconnect / setmaster enable / cd fadeout)
//                        are order-preserving companions that are not value
//                        writes and have no snapshot (documented: they are
//                        accepted non-recoverable side effects of Apply).
//
// The request carries the user's password in memory only: it is never logged,
// never persisted, and never leaves the single "sv_password" write.

#include <cstddef>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "ServerOptions.h"

namespace services {
namespace creategame {

// The map set the page offers. Built from the engine filesystem enumeration
// (module) - the catalog itself is a pure container: names in through
// addRawName (stripped + grammar-checked), finalized into a sorted,
// de-duplicated token list. Contains() is the validator's membership gate, so
// a request map is either an enumerated engine name or refused.
class MapCatalog {
public:
  static constexpr std::size_t kCapacity = 4096;

  bool addRawName(const char *relativeName);
  void finalize();
  void clear();

  std::size_t size() const {
    return Names.size();
  }
  const std::string &nameAt(std::size_t index) const {
    return Names[index];
  }
  bool contains(const std::string &map) const;

  // Launch-side token grammar (runs on every catalog entry AND on the
  // request map): 1..127 bytes, [A-Za-z0-9_-] first, then
  // [A-Za-z0-9_.-/]; no ".." sequence, no empty path segment, no trailing
  // separator - a token can never traverse outside the maps/ tree and can
  // never act as anything but the single argument of `map`.
  static bool validMapToken(const char *map);

private:
  std::vector<std::string> Names;
};

// One create-local-game request: the four values the shipped Server page
// collects (map / maxplayers / name / password).
struct Request {
  std::string Map;
  int MaxPlayers = 0;
  std::string ServerName;
  std::string Password;
  bool Lan = true;
  std::vector<ServerOptionValue> Options;
};

enum class Field {
  kMap,
  kMaxPlayers,
  kServerName,
  kPassword,
};

enum class Error {
  kNone,
  kMapEmpty,
  kMapToken,         // grammar violation (traversal / charset / length)
  kMapNotEnumerated, // well-formed but not in the enumerated set
  kPlayersRange,     // not an integer in [1, 32]
  kNameEmpty,
  kNameLength,     // > 63 bytes
  kNameCharset,    // control / quote / semicolon / backslash
  kPasswordLength, // > 63 bytes
  kPasswordCharset,
};

struct Validation {
  Error error = Error::kNone;
  Field field = Field::kMap;
  bool ok() const {
    return error == Error::kNone;
  }
};

// The production create-game service. Holds the catalog and the launch seams;
// the module injects engine thunks (the offline test records).
class CreateGameModel {
public:
  static constexpr int kMinPlayers = 1;
  static constexpr int kMaxPlayers = 32;           // settings.scr NUMBER 1..32
  static constexpr std::size_t kMaxTextBytes = 63; // shipped strncpy 0x40

  // Request text rule for hostname/password (the validator's charset half).
  // Bytes 0x20..0x7E and >= 0x80 pass (UTF-8 names are legitimate), except
  // '"', ';' and '\\' (command/infostring delimiters) and 0x7F. Password may
  // be empty; hostname may not.
  static bool validTextChar(char c);

  // Strict base-10 parse of the players field text: the WHOLE string must be
  // digits (no sign, no whitespace, no suffix) and the value must fit the
  // [1, 32] range. Empty and overflowing texts refuse - the page's raw field
  // text goes through here before a typed Request exists.
  static bool parsePlayers(const char *text, int &out);

  void setCatalog(MapCatalog catalog) {
    Catalog = std::move(catalog);
  }
  const MapCatalog &catalog() const {
    return Catalog;
  }
  void setOptions(std::vector<ServerOption> Value) {
    Options = std::move(Value);
  }
  const std::vector<ServerOption> &options() const {
    return Options;
  }
  bool validOptions(const Request &Value) const {
    return validateServerOptions(Options, Value.Options);
  }

  static Validation validate(const MapCatalog &catalog, const Request &request);

  // Engine seams. Plain function pointers, no captured state: production
  // wires thin thunks, the offline test records.
  struct Seams {
    // Live cvar string value, or nullptr when the cvar does not exist
    // (fail-closed: a missing cvar can never be snapshotted, its write
    // fails verification and the launch aborts).
    const char *(*readCvar)(const char *name) = nullptr;
    // Typed string write, false when the cvar does not exist (the T10
    // settings seam shape). Never a concatenated command string.
    bool (*writeCvarString)(const char *name, const char *value) = nullptr;
    void (*clientCmd)(const char *command) = nullptr;
  };
  void setSeams(const Seams &seams) {
    Seams_ = seams;
  }
  bool wired() const {
    return Seams_.readCvar && Seams_.writeCvarString && Seams_.clientCmd;
  }

  enum class Launch {
    kLaunched,    // every write verified, the map command left last
    kInvalid,     // validation failed: zero engine calls
    kWriteFailed, // a cvar refused / did not stick; snapshots restored
    kUnwired,     // a seam is missing: zero engine calls
  };

  // Executes the shipped order (format string 0x1007f7a0), split into the
  // recoverable-write and irreversible stages above.
  Launch launch(const Request &request);
  // After the local join, server-DLL cvars exist. Verify the queued options
  // and repair values overwritten by map config through typed writes.
  std::vector<std::string> settleOptions(const Request &Value);

  // The three recoverable cvars in write order (sv_lan, sv_password,
  // hostname) and the evidence of the last launch: the failing cvar index
  // (-1 = none), how many restores failed to read back, and the snapshot
  // values. A snapshot can contain the PREVIOUS sv_password; it is restore
  // machinery only - callers must never log snapshot values.
  static constexpr int kRecoverableCount = 3;
  static const char *recoverableName(int index);
  const std::string &snapshot(int index) const {
    static const std::string kEmpty;
    return index >= 0 && index < kRecoverableCount ? Snapshots[index].Value
                                                   : kEmpty;
  }
  bool snapshotPresent(int index) const {
    return index >= 0 && index < kRecoverableCount && Snapshots[index].Present;
  }
  int lastFailIndex() const {
    return FailIndex;
  }
  int lastRestoreFailures() const {
    return RestoreFailures;
  }

private:
  struct Snapshot {
    bool Present = false;
    std::string Value;
  };

  bool writeAndVerify(int index, const char *value);
  Launch restoreAndFail(int failIndex);

  MapCatalog Catalog;
  std::vector<ServerOption> Options;
  Seams Seams_;
  std::array<Snapshot, kRecoverableCount> Snapshots;
  std::array<bool, kRecoverableCount> Wrote = {};
  int FailIndex = -1;
  int RestoreFailures = 0;
};

} // namespace creategame
} // namespace services
