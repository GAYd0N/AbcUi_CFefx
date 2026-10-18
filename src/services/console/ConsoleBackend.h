#pragma once

// services::console::ConsoleBackend - the user submission channel of the new
// console (T15). Strictly separated from the output direction: this object
// submits ONE engine command line per call and keeps the bounded, redacted
// input history; it never touches the output cache (ConsoleBuffer).
//
// Command semantics (evidence):
//   - Execution rides pfnClientCmd - the exact channel the shipped console
//     entry submitted to (GameUI.dll CGameConsoleDialog "ConsoleSubmit" ->
//     TabCatchingTextEntry handler issues the entered line through the
//     engine client command buffer). The engine's own command/alias chain
//     (including every CChainedCommand the engine knows: slot1..+attack)
//     then consumes it exactly like typed input - this backend never
//     re-implements or bypasses the chain (T15 step 4, coding rule 6).
//   - The engine buffer flushes between frames, so submit() reports the
//     hand-off, not completion; the effect is observed through the output
//     stream like any other command.
//   - Validation is fail-closed: empty (after trimming) is refused, embedded
//     CR/LF is refused (a submission is exactly one command line - the
//     multiline paste requires page confirmation), and byte length is bounded.
//     User-authored semicolon command chains remain supported.
//   - History conservatively omits any line containing "password" (case
//     insensitive), including later commands and quoted alias bodies. These
//     lines still execute; this policy makes no claim to redact engine output.
//   - Completion enumerates the REAL runtime catalogs read-only through the
//     injected seams (cvars via GetFirstCvarPtr chain, commands via the
//     GetFirstCmdFunctionHandle cursor). Names are copied immediately - no
//     engine pointer is kept past the call - and both the scan and the
//     returned matches are capped.
//
// Engine-free by design (function-pointer seams), so the exact production
// translation unit links into the offline acceptance test.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace services::console {

class ConsoleBackend {
public:
  enum class SubmitResult : int {
    kExecuted = 0,        // handed to the engine command channel once
    kExecutedUnknownVerb, // ... and the first verb is unknown to the client
                          // catalog
    kRejectedEmpty,       // nothing but whitespace
    kRejectedInvalid,     // embedded newline / over the byte bound
    kUnavailable,         // no seam or the seam reported the hand-off failed
  };

  static constexpr std::size_t kMaxHistory = 64;
  static constexpr std::size_t kMaxCommandBytes = 255;
  static constexpr std::size_t kMaxCompletions = 256;
  static constexpr std::size_t kMaxEnumeration = 1024; // per source per scan

  struct Seams {
    // Real engine command submit (pfnClientCmd wrapper); returns false
    // when the channel could not take the command.
    bool (*clientCmd)(const char *command) = nullptr;
    // Read-only catalog enumeration: fills up to `max` borrowed name
    // pointers, returns the count. Borrowed = copied by the caller before
    // the next engine call.
    std::size_t (*listCvars)(const char **out, std::size_t max) = nullptr;
    std::size_t (*listCommands)(const char **out, std::size_t max) = nullptr;
  };

  struct Counters {
    std::uint64_t Executed = 0;
    std::uint64_t RejectedEmpty = 0;
    std::uint64_t RejectedInvalid = 0;
    std::uint64_t Unavailable = 0;
    std::uint64_t HistorySkippedSensitive = 0;
  };

  void setSeams(const Seams &seams) {
    Seams_ = seams;
  }

  // The one submission entry. Trims surrounding whitespace, validates, then
  // executes exactly once through the command seam. The result separates
  // "the engine took it and the client knows the verb" from "the engine
  // took it but the client catalog does not know the verb" so the caller
  // can report the latter (the engine itself stays silent for it).
  SubmitResult submit(const char *text);

  // Bounded history, oldest first, newest last. Sensitive commands are
  // absent by construction (counted in the counters instead).
  const std::deque<std::string> &history() const {
    return History;
  }
  void clearHistory() {
    History.clear();
  }
  Counters counters() const {
    return Stats;
  }

  // Case-insensitive prefix completion over cvars + commands: sorted,
  // exact-name deduplicated, capped (both the scan and the result). An
  // empty prefix lists the catalog head.
  std::vector<std::string> complete(const char *prefix, std::size_t max) const;

  // True when the line's FIRST VERB resolves to a known cvar or command
  // (case-insensitive; a leading quoted verb and a ';'/whitespace boundary
  // follow the engine's own tokenizer, and $-substitutions stay "known").
  // The engine executes an unknown client command silently - Sven Co-op's
  // hw.dll carries no unknown-command message - and may forward it to a
  // server, so the caller must only use this to REPORT, never to block.
  // Fail-open: with no catalog seam the answer is true (never claim
  // "unknown" without data). verbOf() returns the same classification's
  // verb ("" when the line carries structure that is not classifiable).
  bool knownVerb(const char *line) const;
  static std::string verbOf(const char *line);

private:
  static bool sensitiveCommand(const std::string &command);
  void collectNames(std::vector<std::string> &out) const;

  Seams Seams_;
  std::deque<std::string> History;
  Counters Stats;
};

// Process-scoped singleton; the production seams ride
// rmlui/VGUI2ExtensionImport.cpp (engine integration layer).
ConsoleBackend &consoleBackend();

} // namespace services::console
