#pragma once

// services::loading::LoadingModel - the value-only projection of one connect
// attempt's engine loading stream (T14.a). Deliberately free of engine, VGUI
// and Rml includes so the exact production logic links into offline tests
// (05 doc tier 1).
//
// Division of truth (T09 "no second truth" rule kept):
//   - The lifecycle truth (phase, attempt generation, error reason) is the
//     T04 MenuController state machine; this model never changes a phase.
//   - This model owns only the PRESENTATION values the engine streams through
//     the IGameUI loading callbacks: the two independent progress channels,
//     the status lines, the resource being loaded, and the once-only cancel
//     latch the page renders from.
//   - Every value is an owned copy; the engine's borrowed pointers never
//     outlive the callback (the module drains a copied queue, LoadingFeed).
//
// Engine evidence (Ghidra, GameUI.dll image base 0x10000000):
//   - The stream arrives through IGameUI slots 12..19 (CGameUI vtable
//     0x10080530): LoadingStarted 0x10006730, LoadingFinished 0x10006690,
//     StartProgressBar 0x100070e0, ContinueProgressBar 0x10006320,
//     StopProgressBar 0x10007220, SetProgressBarStatusText 0x10006bb0,
//     SetSecondaryProgressBar 0x10006bf0, SetSecondaryProgressBarText
//     0x10006c30.
//   - ContinueProgressBar returns int: the shipped implementation answers 1
//     while its dialog exists and 0 once the dialog is gone (0x10006320);
//     hw.dll consumes that answer per loading tick (thunk 0x1d17100 -> call
//     [vtbl+0x3c]; return 0 = the engine skips that frame's continuation).
//     The takeover keeps the vanilla handler running (observed, never
//     superceded - T14 step a.1) and cancels through the proven command
//     instead (see below), so the return value needs no interception.
//   - The vanilla Cancel button issues pfnClientCmd("disconnect\n") - the
//     http-download sub-state issues "httpstop\n" instead (CLoadingDialog
//     handler 0x10007c00) - and then destroys the dialog. The cancel action
//     of this model is that same proven termination command; the HTTP
//     sub-state flag (dialog offset 0x13c) is not observable through the
//     extension-proxied callbacks and is recorded as a limitation (T14.md).
//
// Address grammar (parseAddress): the engine connect command takes
// "host[:port]" (hw.dll "connect %s"; the shipped browser builds the same
// string). Validation is fail-closed and charset-strict because the value is
// handed to the command channel as a payload: letters/digits/dot/dash host
// labels (plus '_' which some server names use), optional numeric port
// 1..65535. Anything else is refused before any engine call.

#include <cstdint>
#include <string>

namespace services {
namespace loading {

// One progress channel. Known=false is the honest "unknown total" state
// (StartProgressBar with steps <= 0 and no fraction yet): the page shows no
// fill and no percentage - never a fabricated animation.
struct LoadingProgress {
  bool Known = false;
  int Point = 0;
  int Steps = 0;
  float Fraction = 0.0f; // 0..1 engine truth (preferred when present)
};

class LoadingModel {
public:
  // Strict "host[:port]" parse; `address` is taken as-is (never normalized
  // away), `outHost` receives the host part, `outPort` the port (0 = none).
  static bool parseAddress(const char *address, std::string &outHost,
                           int &outPort);

  // A new attempt owns the model: generation must be non-zero. Values from
  // the previous attempt are dropped, the address is replaced.
  void startAttempt(uint32_t generation, const char *address);
  // The attempt retired (cancel / success / failure): every later event for
  // it drops. The address is kept so a retry can reuse it.
  void retire();

  // Engine stream. Every entry is generation-filtered: generation 0 or a
  // retired/superseded generation is dropped silently (the cancel and
  // late-arrival rule of T14 step a.3).
  void loadingStarted(uint32_t generation, const char *resourceType,
                      const char *resourceName);
  void loadingFinished(uint32_t generation, const char *resourceType);
  void startProgress(uint32_t generation, const char *progressType, int steps);
  void continueProgress(uint32_t generation, int point, float fraction);
  void stopProgress(uint32_t generation, bool error);
  void setStatusText(uint32_t generation, const char *text);
  void setSecondaryProgress(uint32_t generation, float fraction);
  void setSecondaryProgressText(uint32_t generation, const char *text);

  // Once-only cancel latch for the live generation: true exactly on the
  // first request. The module turns it into the controller's cancel intent
  // (whose phase gate is the second guard) - the backend command itself is
  // issued once by the controller path.
  bool requestCancel(uint32_t generation);

  // The server password prompt of the live attempt (IGameUI +0x58). The
  // per-generation count is the lead line's truth: the first prompt means
  // "required", any later one means the previous password was rejected.
  void notePasswordPrompt(uint32_t generation);
  int passwordPrompts() const {
    return PasswordPrompts_;
  }

  uint32_t generation() const {
    return Generation;
  }
  const std::string &address() const {
    return Address;
  }
  bool canceled() const {
    return Canceled;
  }
  bool levelStarted() const {
    return LevelStarted;
  }
  bool levelFinished() const {
    return LevelFinished;
  }
  const LoadingProgress &primary() const {
    return Primary;
  }
  float secondaryFraction() const {
    return SecondaryFraction;
  }
  const std::string &statusText() const {
    return StatusText;
  }
  const std::string &secondaryText() const {
    return SecondaryText;
  }
  const std::string &resourceText() const {
    return ResourceText;
  }

private:
  bool live(uint32_t generation) const {
    return generation != 0 && generation == Generation;
  }

  uint32_t Generation = 0;
  int PasswordPrompts_ = 0;
  std::string Address;
  bool Canceled = false;
  bool LevelStarted = false;  // LoadingStarted("level") seen
  bool LevelFinished = false; // LoadingFinished("level") seen
  LoadingProgress Primary;
  float SecondaryFraction = 0.0f;
  std::string StatusText;
  std::string SecondaryText;
  std::string ResourceText; // last LoadingStarted resource (name or type)
};

} // namespace loading
} // namespace services
