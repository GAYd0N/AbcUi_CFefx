#pragma once

// services::settings - video mode switch transaction (T12).
//
// Binary evidence (hw.dll, T12 execution record a-gate; addresses are research
// anchors, never hardcoded here):
//   * writes  - `_setvideomode %i %i` (handler 0x01d6e480 -> ScreenWidth/
//     ScreenHeight hardware cvars), `_setrenderer gl windowed|fullscreen`
//     (0x01d6e410 -> ScreenWindowed), `_restart` (0x01d6a390 -> in-process
//     engine restart). All verbatim through pfnClientCmd; the original
//     OptionsSubVideo ApplyChanges (GameUI.dll FUN_1001d3e0) sends exactly
//     this batch and only when the values differ from the page snapshot.
//   * reads   - IGameUIFuncs slots 5/6/7 (GetVideoModes / GetCurrentVideoMode
//     / GetCurrentRenderer; vtable 0x01e78b08): enumerated {w,h,bpp} modes,
//     the ACTUAL current mode, and the windowed flag. vmode_s = 3 x int32
//     ({width,height,bpp}; enumeration array stride 0xc pinned at
//     0x01dbdfa0).
//   * persist - the writes land in the engine's registry-backed hardware
//     settings (HKCU\...\Sven Co-op\Settings; 0x01df4400/0x01df4490/
//     0x01df44e0), applied by the next video init. The engine ALSO keeps its
//     own crash flag (CrashInitializingVideoMode) - this model adds the
//     plugin-level unconfirmed-switch record on top, it does not replace it.
//
// Recovery protocol (execution/T12.md a.5; crash-convergent order):
//   apply()  = snapshot actual(old) -> persist Pending record -> write the
//              CHANGED commands -> `_restart`.
//   after the restart the actual mode is re-read (never trust the request):
//     actual == new  -> confirmation window: keep clears the record; reject
//                       or the monotonic-clock timeout reverts (write old,
//                       clear record, restart).
//     actual == old  -> the switch never took effect: clear the record and
//                       write the old values back (undo the staged cvar
//                       writes for the next launch); no restart needed.
//     unknown/other  -> fail-closed revert path only; keep is refused.
//   crash mid-flight -> the persisted record drives the next launch:
//     recorded new still active -> fresh confirmation window on the real
//     actual mode; anything else (old active, unreadable) -> automatic
//     write-back of the old values + record cleared. The record is cleared
//     only after the revert writes succeeded, so a crash inside revert
//     converges on the same actual==old branch (no loop).
//
// Clock discipline (task b): the in-session confirmation window runs on a
// monotonic clock (steady_clock in production) - focus loss or missing
// frames cannot extend it, the deadline fires on the first tick after it
// expires; the persisted record carries a wall-clock stamp so a record that
// outlived the cap is auto-reverted at startup instead of re-asking.
// Duplicate or late confirmations are inert (generation + state gates).
//
// No engine/Rml includes: offline tests link this exact file with fake seams
// (05 doc evidence tier 1/2). All values crossing a seam are validated here
// first (bounded ints, constants-only command strings).

#include <string>
#include <vector>

namespace services {
namespace settings {

struct VideoMode {
  int Width = 0;
  int Height = 0;
  int Bpp = 0;
  bool Windowed = false;

  bool sameResolution(const VideoMode &other) const {
    return Width == other.Width && Height == other.Height;
  }
  bool operator==(const VideoMode &other) const {
    return sameResolution(other) && Windowed == other.Windowed;
  }
};

// The persisted unconfirmed-switch record ("video_pending" in config.json in
// production via abc::Config's atomic writer). Plain POD - the store adapter
// owns the JSON schema; the model owns the semantics.
struct VideoPendingRecord {
  int Pending = 0; // 1 = unconfirmed switch outstanding
  int NewWidth = 0;
  int NewHeight = 0;
  int NewWindowed = 0;
  int OldWidth = 0;
  int OldHeight = 0;
  int OldWindowed = 0;
  long long AppliedUnixMs = 0; // wall clock at apply (stale-record cap)
  int Generation = 0;          // bumped per apply; stale confirmations lose
};

class VideoModeModel {
public:
  // Fixed policy constants (pinned by the offline tests).
  static constexpr int kConfirmWindowMs = 15000;      // in-session countdown
  static constexpr long long kStaleRecordMs = 600000; // 10 min: auto-revert

#ifndef NDEBUG
  // Debug-only confirmation-window override (diagnostic surface, plan 05:
  // Debug injection is allowed, Release keeps the production policy). Lets
  // a tester observe the keep/reject paths when the transport (RCON channel
  // rebuild) outlasts the production window. 0 = production constant.
  void setConfirmWindowMsForTesting(int milliseconds) {
    ConfirmWindowMs = milliseconds;
  }
#endif
  static constexpr int kMinDimension = 320;
  static constexpr int kMaxDimension = 16384;
  static constexpr int kMaxModes = 256; // enumerated-mode copy cap

  // Backend seams - production wiring injects the vgui2ext query thunks and
  // abc::EngineClientCmd; tests inject an in-memory engine. Function
  // pointers, no captures.
  struct Seams {
    // Actual current mode (IGameUIFuncs slots 6+7). false = unavailable.
    bool (*actualMode)(VideoMode &out) = nullptr;
    // Enumerated modes (slot 5), returns the FULL count; copies up to max.
    int (*modeList)(VideoMode *out, int max) = nullptr;
    // Verbatim engine client command; false = not handed to the engine.
    bool (*clientCmd)(const char *command) = nullptr;
    // Persisted pending record (abc::Config JSON in production).
    bool (*savePending)(const VideoPendingRecord &record) = nullptr;
    bool (*loadPending)(VideoPendingRecord &out) = nullptr;
    // Monotonic seconds since process start (steady_clock in production).
    double (*monotonicSeconds)() = nullptr;
    // Wall clock ms since the Unix epoch (system_clock in production).
    long long (*wallMs)() = nullptr;
  };

  void setSeams(const Seams &seams) {
    Seams_ = seams;
  }
  // Queries + persistence reachable. Without it the whole section is
  // fail-closed (nothing staged, nothing applied, nothing confirmed).
  bool available() const;

  // Re-reads the actual mode and the enumerated list. Safe anytime.
  void snapshot();
  const VideoMode &actual() const {
    return Actual;
  }
  const std::vector<VideoMode> &modes() const {
    return Modes;
  }
  bool actualKnown() const {
    return ActualKnown;
  }

  // Staged (draft) request. Resolution must be a non-negative pair present
  // in the enumerated list (or the actual mode); windowed is a free toggle.
  // Staging writes nothing.
  bool stageResolution(int width, int height);
  bool stageWindowed(bool windowed);
  void cancelStage();
  bool hasStaged() const {
    return Staged.ResolutionSet || Staged.WindowedSet;
  }
  // The effective target of the current stage (staged facets over the
  // actual mode). Meaningful while hasStaged().
  VideoMode stagedTarget() const;

  // Lifecycle. kIdle -> (stage) -> kStaged -> (apply) -> kAwaitingConfirm ->
  // (confirm/revert/timeout) -> kIdle. A persisted pending record found at
  // startup enters through resolveRecovery().
  enum class State { kIdle, kStaged, kAwaitingConfirm };
  State state() const {
    return State_;
  }

  // Issues the switch: persist-record-first, then the changed commands and
  // `_restart`. Refuses (with no writes) when unavailable, nothing staged,
  // the stage already matches the actual mode, or a confirmation is
  // outstanding. Returns true when the batch was handed to the engine.
  bool apply();

  // --- Startup recovery -----------------------------------------------
  // Loads the persisted record if one exists.
  //   kPending      - a recent, still-applied record waits for its
  //                   confirmation window (caller presents the page);
  //   kAutoReverted - the record was unresolved (not applied, unknown or
  //                   too stale) and the old values were written back,
  //                   restarting only when the actual mode required it;
  //   kAccepted     - reserved legacy enum value, no longer emitted:
  //                   elapsed wall time never implies user confirmation;
  //   kNoRecord     - nothing pending;
  //   kDeferred     - the actual mode cannot be queried right now
  //                   (missing extension): the record is KEPT for a later
  //                   session, nothing is written.
  enum class RecoveryResult {
    kNoRecord,
    kPending,
    kAutoReverted,
    kAccepted,
    kDeferred
  };
  RecoveryResult resolveRecovery();

  // The persisted record is loaded but the actual mode is re-read only in
  // resolveRecovery(); while kAwaitingConfirm these describe the outcome
  // for the confirmation page.
  bool awaitingConfirm() const {
    return State_ == State::kAwaitingConfirm;
  }
  const VideoPendingRecord &pending() const {
    return Record;
  }
  // What the confirmation is actually looking at (actual vs record).
  enum class ConfirmKind {
    kApplied,    // actual == new: keep clears, revert restores old
    kNotApplied, // actual == old: keep is invalid; resolve = write-back + clear
    kUnknown,    // actual unreadable or matches neither: revert-only
  };
  // Re-queries the ACTUAL current mode first (the engine, not the request -
  // the page must show the real mode after every restart), then classifies.
  ConfirmKind confirmKind();

  // Seconds left in the monotonic confirmation window (0 when not awaiting).
  int remainingSeconds() const;

  // Keep the new mode. Valid only for kApplied (clears the record and the
  // awaiting state); any other kind or state is inert and returns false.
  bool confirm();

  // Restore the previous mode: write the old values, clear the record,
  // `_restart`. For kNotApplied the write-back happens without a restart
  // (the new mode never became active). Idempotent per generation.
  bool revert();

  // Monotonic deadline beat - call every frame while awaiting. When the
  // window expired this performs revert() and returns true (deadline fires
  // on the first tick after expiry; hidden/unfocused UI cannot extend it).
  bool tick();

  // The exact command strings apply()/revert() would issue, in order -
  // pinned by the offline tests. pendingRevertCommands() re-queries the
  // live actual mode first (same rule as confirmKind()).
  std::vector<std::string> pendingApplyCommands() const;
  std::vector<std::string> pendingRevertCommands();

  // One-line description of a mode ("1920 x 1080 windowed"), for the
  // page/model diagnostics. Pure formatting, no engine access.
  static std::string describe(const VideoMode &mode);

private:
  struct StagedMode {
    VideoMode Mode;
    bool ResolutionSet = false;
    bool WindowedSet = false;
  };

  int windowMs() const {
    return ConfirmWindowMs > 0 ? ConfirmWindowMs : kConfirmWindowMs;
  }
  bool validDimension(int value) const;
  bool resolutionInList(int width, int height) const;
  void writeMode(const VideoMode &mode, bool changedResolution,
                 bool changedWindowed) const;
  bool writeOldValues(const VideoPendingRecord &record, bool includeResolution,
                      bool includeWindowed);
  bool persist(const VideoPendingRecord &record);
  void beginAwaiting(const VideoPendingRecord &record);

  Seams Seams_;
  VideoMode Actual;
  bool ActualKnown = false;
  std::vector<VideoMode> Modes;

  State State_ = State::kIdle;
  StagedMode Staged;
  // While awaiting: the record being confirmed and the monotonic deadline
  // (armed on the first tick after the awaiting state was entered).
  VideoPendingRecord Record;
  int ConfirmWindowMs = 0; // 0 = kConfirmWindowMs (production)
  bool DeadlineArmed = false;
  bool RevertCommandsIssued = false;
  double DeadlineSeconds = 0.0;
  // Expired-window revert retry throttle (a refused write is retried once
  // per second, never once per frame).
  double NextRevertAttemptSeconds = 0.0;
  int Generation = 0;
};

} // namespace settings
} // namespace services
