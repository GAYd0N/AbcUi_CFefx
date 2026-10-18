#include "AutoRetryGate.h"

namespace services::loading {

bool AutoRetryGate::tick(double deltaSeconds, bool enabled, bool failed) {
  if (!enabled || !failed) {
    // Not trying right now: nothing to fire. The budget deliberately
    // survives a brief "trying again" window (the caller's reset() owns
    // the terminal outcomes).
    return false;
  }
  if (Attempts >= kMaxAttempts) {
    Delay = -1.0; // the chain is done; the error surface stays
    return false;
  }
  if (Delay < 0.0) {
    Delay = kDelaySeconds; // arm the first (or next) retry
    return false;
  }
  if (deltaSeconds > 0.0)
    Delay -= deltaSeconds;
  if (Delay > 0.0)
    return false;
  Delay = -1.0;
  ++Attempts;
  return true;
}

void AutoRetryGate::reset() {
  Attempts = 0;
  Delay = -1.0;
}

} // namespace services::loading
