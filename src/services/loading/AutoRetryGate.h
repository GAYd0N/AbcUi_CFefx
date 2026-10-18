#pragma once

// services::loading::AutoRetryGate - the pure run policy behind the detail
// window's 自动重试 toggle: while it is armed, a failed join (the typical
// case being a FULL server, whose slots free up on their own schedule) keeps
// re-entering the SAME typed retry path, bounded per failure chain and paced
// by a delay. Deliberately free of engine/Rml includes so the exact policy
// links into the offline tests (05 doc tier 1).
//
// Contract:
//   - tick(deltaSeconds, enabled, failed) answers only one question: "retry
//     now?". It never performs the retry and never touches engine state.
//   - A chain is armed when the toggle is on AND the failure surface is up;
//     the first retry fires one delay after the failure, the next ones one
//     delay after each new failure.
//   - The budget is bounded: at most kMaxAttempts consecutive auto-retries
//     per chain, then the gate stays quiet (the honest error surface remains
//     the player's decision). reset() starts a fresh chain (a confirmed
//     join, a cancel/dismissal, a manual retry, the toggle going off, or a
//     fresh user-initiated attempt).
//   - A disabled gate or a non-failed surface resets nothing by itself: the
//     caller owns the terminal outcomes, so a fired retry (which briefly
//     leaves the failure state while the new attempt runs) cannot silently
//     refill the budget.

#include <cstdint>

namespace services::loading {

class AutoRetryGate {
public:
  // A full server may free a slot minutes later, so the chain is patient
  // but still bounded (the player can cancel at any point, which resets).
  static constexpr int kMaxAttempts = 10;
  static constexpr double kDelaySeconds = 5.0;

  // One frame of the policy. Returns true exactly on the frame the caller
  // must issue the retry.
  bool tick(double deltaSeconds, bool enabled, bool failed);

  // Start a fresh chain (budget restored, timer cleared).
  void reset();

  int attempts() const {
    return Attempts;
  }
  // False once the chain's budget is exhausted (the gate no longer tries).
  bool retrying() const {
    return Delay >= 0.0 && Attempts < kMaxAttempts;
  }

private:
  int Attempts = 0;
  double Delay = -1.0; // < 0 = not armed
};

} // namespace services::loading
