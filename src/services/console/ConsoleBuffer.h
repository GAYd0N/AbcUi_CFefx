#pragma once

// services::console::ConsoleBuffer / ConsoleCapture - the bounded mirror of
// the engine console text stream (T15). Engine-free by design (plain data
// in, no VGUI/extension includes) so the exact production bookkeeping links
// into the offline acceptance test (05 doc tier 1).
//
// Evidence for the shapes below (Ghidra, install binaries hw.dll e3c7f374 /
// GameUI.dll 99382b87; the extension proxy at pin b290930):
//   - IGameConsole::Printf/DPrintf are vtable slots 6/7 of the GameUI.dll
//     instance (hardware: hw.dll FUN_01d16e00 calls [vtbl+0x18] for the
//     Con_Printf path, FUN_01d16e90 [vtbl+0x1c] for Con_DPrintf). The
//     VGUI2Extension wraps both and dispatches pre AND post per call with a
//     fresh Result (thirdparty/VGUI2Extension/src/GameUI.cpp:2143-2233) -
//     one engine message therefore reaches the plugin twice, and the
//     wrapper keeps ONE IVGUI2Extension_String object across the pair
//     (that identity is what onPrintf/onDPrintf dedup on).
//   - The engine's developer gate sits ABOVE the vtable call: Con_DPrintf
//     (hw.dll FUN_01d03b90) tests the developer float before it reaches
//     IGameConsole::DPrintf, so a DPrintf callback only ever fires in
//     developer mode - the flag carried into the buffer keeps that
//     distinction for the UI.
//   - The -condebug file log (hw.dll FUN_01d03e40, console-YYYY-MM-DD.log)
//     is written by the engine's own print paths before the vtable call.
//     This capture never prints (a ConsolePrint from inside the callback
//     would re-enter it AND double-write that file): it copies only.
//   - IGameConsole::Clear is vtable slot 4 (extension proxy
//     GameUI.cpp:2073-2096); the engine's `clear` command handler
//     (hw.dll 0x01d40b70, registered 0x01d40930) wipes the notify context,
//     then calls the console Clear. Clear is dispatched pre+post as well.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace services::console {

// One complete console line. Text is the exact byte copy of what the engine
// printed (UTF-8 passthrough - printable non-ASCII survives untouched),
// without the trailing newline. Sequence is monotonic for the process and is
// never reused, so a consumer can page forward (copySince) across Clear.
struct ConsoleLine {
  std::string Text;
  std::uint64_t Sequence = 0;
  bool Developer = false; // assembled from DPrintf fragments
  bool Truncated = false; // hit kMaxLineBytes; the remainder was cut
};

struct ConsoleBufferStats {
  std::uint64_t Fragments = 0;             // pushText calls seen
  std::uint64_t DroppedBytes = 0;          // bytes cut by the line byte cap
  std::uint64_t DroppedLines = 0;          // lines evicted by the line cap
  std::uint64_t RejectedAfterShutdown = 0; // pushes after shutdown()
};

// A bounded tail of the engine console. Two producers feed it: the engine
// capture (above) and the replacement console's own local lines - the same
// messages the vanilla console printed into its scrollback (the unknown
// command answer). Fixed caps (the frozen T15 record):
//   - kMaxLines complete lines; a newer line evicts the oldest;
//   - kMaxLineBytes bytes per line; the overflow is discarded (UTF-8-safe
//     cut) and the line is finalized marked Truncated when its newline
//     arrives.
// Producers may be concurrent (the mutex is real); consumers poll copySince
// with a bounded window. Fragment assembly mirrors the engine console: text
// arrives in pieces, '\n' completes a line, '\r' is ignored (CRLF-safe
// across call boundaries).
class ConsoleBuffer {
public:
  static constexpr std::size_t kMaxLines = 1024;
  static constexpr std::size_t kMaxLineBytes = 1024;

  // Producer entry (engine callback side). Copies immediately; the caller's
  // buffer never outlives the call. `developer` marks a DPrintf fragment:
  // any dev fragment marks the whole assembled line.
  void pushText(const char *text, std::size_t length, bool developer);

  // Clear semantics (engine IGameConsole::Clear): drop every line AND the
  // pending fragment, bump the generation. The sequence counter keeps
  // counting - a consumer watching generation() knows to reset its view.
  void clear();

  // Teardown, called ONCE after the producers were stopped (callbacks
  // unregistered / capture detached) - never before. Idempotent. After
  // this, pushText is refused and counted.
  void shutdown();

  bool stopped() const;
  std::uint64_t generation() const;
  std::uint64_t latestSequence() const;
  std::size_t lineCount() const;
  ConsoleBufferStats stats() const;

  // Copies up to `max` lines newer than `since` into `out` (cleared first).
  // When more lines qualify than `max` (an unbounded cache cannot page a
  // lagging consumer from the start), the NEWEST `max` are returned.
  // Returns the number copied.
  std::size_t copySince(std::uint64_t since, std::vector<ConsoleLine> &out,
                        std::size_t max) const;

private:
  void finalizeLineLocked();
  void trimPartialSequenceLocked();

  mutable std::mutex Mutex;
  std::deque<ConsoleLine> Lines;
  std::string Pending;
  bool PendingDeveloper = false;
  bool PendingTruncated = false;
  std::uint64_t SequenceCounter = 0;
  std::uint64_t GenerationCounter = 1;
  bool Stopped = false;
  ConsoleBufferStats Stats;
};

// The producer-side seam: one capture per engine console, fed by the
// extension callbacks in rmlui/VGUI2ExtensionImport.cpp. Deduplicates the
// proxy's pre+post double dispatch per message (wrapper string identity) so
// ONE engine message becomes ONE cached line, whatever combination of
// phases actually ran (a higher-altitude plugin may supersede the pre phase,
// or the post phase may be skipped). Main-thread-only by construction (the
// engine UI callbacks run there); the buffer itself carries the lock.
class ConsoleCapture {
public:
  // Per-message-key (wrapper string object, method, byte length) seen in
  // the pre phase, waiting for its post echo. Small and fixed: the real
  // dispatch is pre->post adjacent; the entries tolerate nested re-entry
  // (a plugin printing during dispatch) without double-storing.
  static constexpr std::size_t kMaxPending = 8;

  explicit ConsoleCapture(ConsoleBuffer &buffer) : Buffer(buffer) {}

  // `source` is the wrapper's IVGUI2Extension_String object identity
  // (opaque here); `data`/`length` the formatted text. `post` is the
  // proxy phase.
  void onPrintf(const void *source, const char *data, std::size_t length,
                bool post);
  void onDPrintf(const void *source, const char *data, std::size_t length,
                 bool post);
  void onClear(bool post);

  // Producer stop/start. detach() makes every later callback a no-op (the
  // teardown order: detach -> unregister -> buffer.shutdown()). attach()
  // exists for the Debug dump seam (printing cached text through the
  // engine console would otherwise loop back into the capture).
  void detach();
  void attach();
  bool attached() const {
    return !Detached;
  }

private:
  enum : std::uint8_t { kPrintf = 0, kDPrintf = 1 };

  struct PendingKey {
    const void *Source;
    std::uint8_t Kind;
    std::size_t Length;
  };

  void capture(const void *source, const char *data, std::size_t length,
               bool post, std::uint8_t kind);

  ConsoleBuffer &Buffer;
  std::vector<PendingKey> Pending;
  // One Clear dispatch pair: the pre phase clears and arms the flag; the
  // post phase consumes it. A post without an armed flag means the pre was
  // superseded upstream - that post still clears (once).
  bool ClearPreSeen = false;
  bool Detached = false;
};

// Process-scoped singletons (the plugin TU lifetime). consoleCapture() is
// wired to consoleBuffer(): the engine stream and its cache are one object
// pair for the whole engine session.
ConsoleBuffer &consoleBuffer();
ConsoleCapture &consoleCapture();

} // namespace services::console
