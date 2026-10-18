// services::settings::VideoModeModel implementation - see VideoModeModel.h.
#include "VideoModeModel.h"

#include <cmath>
#include <cstdio>

namespace services {
namespace settings {

namespace {

// Renderer name argument of `_setrenderer` - the engine only ships gl
// (evidence: T12 a-gate; argv[1] is compared against "gl" and discarded).
constexpr const char *kRendererName = "gl";

std::string resolutionCommand(int width, int height) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "_setvideomode %d %d\n", width, height);
  return buffer;
}

std::string rendererCommand(bool windowed) {
  return windowed ? "_setrenderer gl windowed\n"
                  : "_setrenderer gl fullscreen\n";
}

constexpr const char *kRestartCommand = "_restart\n";

VideoMode recordNew(const VideoPendingRecord &record) {
  VideoMode mode;
  mode.Width = record.NewWidth;
  mode.Height = record.NewHeight;
  mode.Windowed = record.NewWindowed != 0;
  return mode;
}

VideoMode recordOld(const VideoPendingRecord &record) {
  VideoMode mode;
  mode.Width = record.OldWidth;
  mode.Height = record.OldHeight;
  mode.Windowed = record.OldWindowed != 0;
  return mode;
}

} // namespace

bool VideoModeModel::available() const {
  return Seams_.actualMode && Seams_.modeList && Seams_.clientCmd &&
         Seams_.savePending && Seams_.loadPending && Seams_.monotonicSeconds &&
         Seams_.wallMs;
}

bool VideoModeModel::validDimension(int value) const {
  return value >= kMinDimension && value <= kMaxDimension;
}

void VideoModeModel::snapshot() {
  if (!Seams_.actualMode)
    return;
  VideoMode mode;
  if (Seams_.actualMode(mode)) {
    Actual = mode;
    ActualKnown = mode.Width > 0 && mode.Height > 0;
  } else {
    ActualKnown = false;
  }
  Modes.clear();
  if (Seams_.modeList) {
    VideoMode buffer[kMaxModes];
    const int count = Seams_.modeList(buffer, kMaxModes);
    const int capped = count < 0 ? 0 : (count > kMaxModes ? kMaxModes : count);
    Modes.assign(buffer, buffer + capped);
  }
}

bool VideoModeModel::resolutionInList(int width, int height) const {
  if (ActualKnown && Actual.Width == width && Actual.Height == height)
    return true;
  for (const auto &mode : Modes) {
    if (mode.Width == width && mode.Height == height)
      return true;
  }
  return false;
}

VideoMode VideoModeModel::stagedTarget() const {
  VideoMode target = Actual;
  if (Staged.ResolutionSet) {
    target.Width = Staged.Mode.Width;
    target.Height = Staged.Mode.Height;
    target.Bpp = Staged.Mode.Bpp;
  }
  if (Staged.WindowedSet)
    target.Windowed = Staged.Mode.Windowed;
  return target;
}

bool VideoModeModel::stageResolution(int width, int height) {
  if (!available() || !ActualKnown || State_ == State::kAwaitingConfirm)
    return false; // no confirmation gates the mode; unknown actual cannot stage
  if (!validDimension(width) || !validDimension(height))
    return false;
  if (!resolutionInList(width, height))
    return false; // injection safety: only enumerated (or actual) modes
  Staged.Mode.Width = width;
  Staged.Mode.Height = height;
  Staged.ResolutionSet = true;
  State_ = State::kStaged;
  return true;
}

bool VideoModeModel::stageWindowed(bool windowed) {
  if (!available() || !ActualKnown || State_ == State::kAwaitingConfirm)
    return false;
  Staged.Mode.Windowed = windowed;
  Staged.WindowedSet = true;
  State_ = State::kStaged;
  return true;
}

void VideoModeModel::cancelStage() {
  Staged = StagedMode{};
  if (State_ == State::kStaged)
    State_ = State::kIdle;
}

std::vector<std::string> VideoModeModel::pendingApplyCommands() const {
  std::vector<std::string> commands;
  if (State_ != State::kStaged || !ActualKnown)
    return commands;
  const VideoMode target = stagedTarget();
  const bool changedResolution =
      Staged.ResolutionSet && !target.sameResolution(Actual);
  const bool changedWindowed =
      Staged.WindowedSet && target.Windowed != Actual.Windowed;
  if (!changedResolution && !changedWindowed)
    return commands;
  if (changedResolution)
    commands.push_back(resolutionCommand(target.Width, target.Height));
  if (changedWindowed)
    commands.push_back(rendererCommand(target.Windowed));
  commands.push_back(kRestartCommand);
  return commands;
}

bool VideoModeModel::apply() {
  if (!available() || State_ != State::kStaged || !ActualKnown)
    return false;
  const VideoMode target = stagedTarget();
  const bool changedResolution =
      Staged.ResolutionSet && !target.sameResolution(Actual);
  const bool changedWindowed =
      Staged.WindowedSet && target.Windowed != Actual.Windowed;
  if (!changedResolution && !changedWindowed)
    return false; // target already matches: nothing staged changes

  // The record exists BEFORE the engine receives a single command: a crash
  // anywhere after this point is recovered from it on the next launch.
  VideoPendingRecord record;
  record.Pending = 1;
  record.NewWidth = target.Width;
  record.NewHeight = target.Height;
  record.NewWindowed = target.Windowed ? 1 : 0;
  record.OldWidth = Actual.Width;
  record.OldHeight = Actual.Height;
  record.OldWindowed = Actual.Windowed ? 1 : 0;
  record.AppliedUnixMs = Seams_.wallMs();
  record.Generation = Generation + 1;
  if (!persist(record))
    return false; // cannot promise recovery: no writes, stay staged

  const std::vector<std::string> commands = pendingApplyCommands();
  bool ok = true;
  for (const auto &command : commands) {
    if (!Seams_.clientCmd(command.c_str())) {
      ok = false;
      break;
    }
  }
  if (!ok) {
    // Roll back the staged cvar writes so the next launch cannot apply
    // half a switch; the record only clears once that write-back landed.
    Generation = record.Generation;
    beginAwaiting(record);
    Staged = StagedMode{};
    if (writeOldValues(record, changedResolution, changedWindowed)) {
      RevertCommandsIssued = true;
      record.Pending = 0;
      if (persist(record))
        State_ = State::kIdle;
    }
    return false;
  }

  Generation = record.Generation;
  beginAwaiting(record);
  Staged = StagedMode{};
  return true;
}

void VideoModeModel::beginAwaiting(const VideoPendingRecord &record) {
  Record = record;
  State_ = State::kAwaitingConfirm;
  DeadlineArmed = false;
  DeadlineSeconds = 0.0;
  NextRevertAttemptSeconds = 0.0;
  RevertCommandsIssued = false;
}

bool VideoModeModel::persist(const VideoPendingRecord &record) {
  return Seams_.savePending(record);
}

// Writes the recorded OLD values back (undoing the staged cvar writes).
// includeResolution/includeWindowed select the facets that were changed.
bool VideoModeModel::writeOldValues(const VideoPendingRecord &record,
                                    bool includeResolution,
                                    bool includeWindowed) {
  bool ok = true;
  if (includeResolution &&
      !Seams_.clientCmd(
          resolutionCommand(record.OldWidth, record.OldHeight).c_str()))
    ok = false;
  if (includeWindowed &&
      !Seams_.clientCmd(rendererCommand(record.OldWindowed != 0).c_str()))
    ok = false;
  return ok;
}

VideoModeModel::RecoveryResult VideoModeModel::resolveRecovery() {
  if (!Seams_.loadPending)
    return RecoveryResult::kNoRecord;
  VideoPendingRecord record;
  if (!Seams_.loadPending(record) || record.Pending == 0)
    return RecoveryResult::kNoRecord;
  if (!available())
    return RecoveryResult::kDeferred; // no read/write path: keep the record
  snapshot();
  if (!ActualKnown || !Seams_.actualMode)
    return RecoveryResult::kDeferred; // keep the record for a later session

  long long age = Seams_.wallMs() - record.AppliedUnixMs;
  if (age < 0)
    age = 0;
  const bool isNew = Actual == recordNew(record);
  const bool isOld = Actual == recordOld(record);

  if (isNew && age <= kStaleRecordMs) {
    Generation = record.Generation;
    beginAwaiting(record);
    return RecoveryResult::kPending;
  }

  // Not applied (or an unknown mode): restore the old values. The record is
  // cleared only after the write-back landed - a crash in between keeps the
  // record and converges on this same branch next launch.
  if (!writeOldValues(record, true, true))
    return RecoveryResult::kDeferred; // write-back refused: keep the record
  if (!isOld) {
    // The session is on neither the old nor the new mode: the running
    // mode must be re-initialized from the restored registry values.
    if (!Seams_.clientCmd(kRestartCommand))
      return RecoveryResult::kDeferred;
  }
  Generation = record.Generation;
  const VideoPendingRecord Pending = record;
  record.Pending = 0;
  if (!persist(record)) {
    beginAwaiting(Pending);
    RevertCommandsIssued = true;
    return RecoveryResult::kDeferred;
  }
  State_ = State::kIdle;
  DeadlineArmed = false;
  return RecoveryResult::kAutoReverted;
}

VideoModeModel::ConfirmKind VideoModeModel::confirmKind() {
  if (State_ != State::kAwaitingConfirm)
    return ConfirmKind::kUnknown;
  // The confirmation always classifies the LIVE mode: after the restart the
  // engine's actual state is the only truth (the page shows it, not the
  // request). The pure query is cheap (a vtable call per render pass).
  if (Seams_.actualMode) {
    VideoMode live;
    if (Seams_.actualMode(live) && live.Width > 0 && live.Height > 0) {
      Actual = live;
      ActualKnown = true;
    }
  }
  if (!ActualKnown)
    return ConfirmKind::kUnknown;
  if (Actual == recordNew(Record))
    return ConfirmKind::kApplied;
  if (Actual == recordOld(Record))
    return ConfirmKind::kNotApplied;
  return ConfirmKind::kUnknown;
}

int VideoModeModel::remainingSeconds() const {
  if (State_ != State::kAwaitingConfirm)
    return 0;
  if (!DeadlineArmed)
    return windowMs() / 1000;
  const double left = DeadlineSeconds - Seams_.monotonicSeconds();
  if (left <= 0.0)
    return 0;
  return static_cast<int>(std::ceil(left));
}

bool VideoModeModel::confirm() {
  if (State_ != State::kAwaitingConfirm || RevertCommandsIssued ||
      confirmKind() != ConfirmKind::kApplied)
    return false; // keep is only valid on the applied-new branch
  if (DeadlineArmed && Seams_.monotonicSeconds() >= DeadlineSeconds)
    return false; // a click before the next tick cannot confirm an expired mode
  VideoPendingRecord record = Record;
  record.Pending = 0;
  if (!persist(record))
    return false; // still outstanding on disk: stay awaiting
  State_ = State::kIdle;
  DeadlineArmed = false;
  return true;
}

std::vector<std::string> VideoModeModel::pendingRevertCommands() {
  // Refresh the live actual mode once, then use that same answer.
  const ConfirmKind kind = confirmKind();
  std::vector<std::string> commands;
  if (State_ != State::kAwaitingConfirm)
    return commands;
  const VideoMode oldMode = recordOld(Record);
  if (kind == ConfirmKind::kApplied) {
    // The old values are written back and the engine restarts into them.
    if (!oldMode.sameResolution(recordNew(Record)))
      commands.push_back(resolutionCommand(oldMode.Width, oldMode.Height));
    if (oldMode.Windowed != recordNew(Record).Windowed)
      commands.push_back(rendererCommand(oldMode.Windowed));
    commands.push_back(kRestartCommand);
  } else if (kind == ConfirmKind::kNotApplied) {
    // The session already runs the old mode: restore the registry/cvar
    // values the staged switch left behind; no restart needed.
    commands.push_back(resolutionCommand(oldMode.Width, oldMode.Height));
    commands.push_back(rendererCommand(oldMode.Windowed));
  } else {
    // Unknown actual: restore both facets and re-initialize.
    commands.push_back(resolutionCommand(oldMode.Width, oldMode.Height));
    commands.push_back(rendererCommand(oldMode.Windowed));
    commands.push_back(kRestartCommand);
  }
  return commands;
}

bool VideoModeModel::revert() {
  if (State_ != State::kAwaitingConfirm)
    return false; // duplicate / late revert is inert
  // Clear-before-restart would lose the record if the restart failed; the
  // crash-convergent order is writes first, record clear only on success.
  if (!RevertCommandsIssued) {
    const std::vector<std::string> commands = pendingRevertCommands();
    for (const auto &command : commands) {
      if (!Seams_.clientCmd(command.c_str()))
        return false; // pending on disk; retry or next-launch recovery
    }
    RevertCommandsIssued = true;
  }
  VideoPendingRecord record = Record;
  record.Pending = 0;
  if (!persist(record))
    return false; // retry persistence, not the already queued restart
  State_ = State::kIdle;
  DeadlineArmed = false;
  return true;
}

bool VideoModeModel::tick() {
  if (State_ != State::kAwaitingConfirm)
    return false;
  const double now = Seams_.monotonicSeconds();
  if (!DeadlineArmed) {
    DeadlineArmed = true;
    DeadlineSeconds = now + windowMs() / 1000.0;
    return false;
  }
  if (now < DeadlineSeconds)
    return false;
  // Deadline passed: attempt the revert. A refused write keeps the awaiting
  // state (the record survives) and retries once per second, never per frame.
  if (now < NextRevertAttemptSeconds)
    return false;
  NextRevertAttemptSeconds = now + 1.0;
  return revert();
}

std::string VideoModeModel::describe(const VideoMode &mode) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%d x %d %s", mode.Width, mode.Height,
                mode.Windowed ? "windowed" : "fullscreen");
  return buffer;
}

} // namespace settings
} // namespace services
