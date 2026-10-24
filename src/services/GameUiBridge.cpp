// services::GameUiBridge implementation - see GameUiBridge.h for the contract.
// T08: the native-UI suppression rules; deliberately engine/VGUI/Rml-free so
// the exact production logic links into the offline acceptance test.
#include "GameUiBridge.h"

namespace services {

namespace {

// ASCII case-insensitive equality; the module names are engine literals
// ("GameUI", "ServerBrowser"), never localized.
bool equalsIgnoreCase(const char *a, const char *b) {
  if (!a || !b)
    return false;
  while (*a && *b) {
    char ca = *a++;
    char cb = *b++;
    if (ca >= 'A' && ca <= 'Z')
      ca = static_cast<char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z')
      cb = static_cast<char>(cb - 'A' + 'a');
    if (ca != cb)
      return false;
  }
  return *a == *b;
}

PaintBucket bucketFor(const char *module) {
  if (equalsIgnoreCase(module, "GameUI"))
    return PaintBucket::kGameUi;
  if (equalsIgnoreCase(module, "ServerBrowser"))
    return PaintBucket::kServerBrowser;
  return PaintBucket::kOther;
}

} // namespace

bool NativeUiBridge::nativeUiModule(const char *module) {
  return equalsIgnoreCase(module, "GameUI") ||
         equalsIgnoreCase(module, "ServerBrowser");
}

void NativeUiBridge::arm() {
  Armed = true;
  // Deliver a pre-arm console entry exactly once (the startup -console /
  // -toconsole path, see the header note). Close wins when both latched.
  if (PendingConsoleOpen || PendingConsoleClose) {
    const bool open = PendingConsoleOpen && !PendingConsoleClose;
    PendingConsoleOpen = false;
    PendingConsoleClose = false;
    if (open)
      requestConsoleOpen(NativeUiOrigin::kGameConsole);
  }
}

void NativeUiBridge::disarm() {
  Armed = false;
  // Teardown: no pending intent may survive into a later arm.
  PendingConsoleOpen = false;
  PendingConsoleClose = false;
}

bool NativeUiBridge::gatePaintAttempt(const char *module) {
  const PaintBucket bucket = bucketFor(module);
  ++Counters.PaintAttempts[static_cast<std::size_t>(bucket)];
  // The module rule is the whole filter; while disarmed every attempt
  // passes (a half-installed gate must not half-suppress - T08 step 7's
  // atomic install).
  if (!armed() || !nativeUiModule(module))
    return false;
  ++Counters.PaintBlocked[static_cast<std::size_t>(bucket)];
  return true;
}

bool NativeUiBridge::gateHitTestAttempt(const char *module) {
  const PaintBucket bucket = bucketFor(module);
  ++Counters.HitTestAttempts[static_cast<std::size_t>(bucket)];
  if (!armed() || !nativeUiModule(module))
    return false;
  ++Counters.HitTestBlocked[static_cast<std::size_t>(bucket)];
  return true;
}

bool NativeUiBridge::requestMenuUp(NativeUiOrigin origin) {
  ++Counters.ActivateRequests[static_cast<std::size_t>(origin)];
  if (MenuUp)
    return false;
  MenuUp = true;
  // A new menu session re-arms the physical-press guard (a release missed
  // during a focus switch must not swallow the next close press).
  EscPressRouted = false;
  ++Counters.MenuUpTransitions;
  if (MenuSink)
    MenuSink->onNativeMenuShow();
  return true;
}

bool NativeUiBridge::requestMenuDown(NativeUiOrigin origin) {
  ++Counters.HideRequests[static_cast<std::size_t>(origin)];
  // T16: everything the menu carried goes with it - the console
  // presentation truth cannot survive its host (a later IsConsoleVisible
  // must not report a console whose overlay is gone).
  ConsoleRequested = false;
  if (!MenuUp)
    return false;
  MenuUp = false;
  ++Counters.MenuDownTransitions;
  if (MenuSink)
    MenuSink->onNativeMenuHide();
  return true;
}

bool NativeUiBridge::requestConsoleOpen(NativeUiOrigin origin) {
  // Pre-arm entries are latched, never presented: the takeover has not
  // installed its gate yet, so the vanilla path stays authoritative until
  // arm() delivers the intent (T08 atomic-install rule).
  if (!armed()) {
    PendingConsoleOpen = true;
    return false;
  }
  // Vanilla shape: the console lives inside GameUI, so opening it raises
  // the host first (the engine's own toggle path calls ActivateGameUI and
  // then ShowConsole). Idempotent: a second open intent (the pre+post
  // dispatch pair included) changes nothing and never re-notifies the
  // sink (the module would otherwise toggle the overlay twice).
  if (!MenuUp)
    requestMenuUp(origin);
  const bool transition = !ConsoleRequested;
  ConsoleRequested = true;
  if (transition && MenuSink)
    MenuSink->onNativeConsoleOpen();
  return transition;
}

bool NativeUiBridge::requestConsoleClose(NativeUiOrigin) {
  if (!armed()) {
    PendingConsoleClose = true;
    return false;
  }
  if (!ConsoleRequested)
    return false;
  ConsoleRequested = false;
  if (MenuSink)
    MenuSink->onNativeConsoleClose();
  return true;
}

void NativeUiBridge::noteConsoleDismissed() {
  // UI-side dismissal (routed ESC through the module's owner, the close
  // button): the module already took the overlay down; this only keeps the
  // engine-facing truth in step. Idempotent.
  ConsoleRequested = false;
}

bool NativeUiBridge::consumeEscRoute() {
  if (EscPressRouted) {
    ++Counters.EscRepeatsSuppressed;
    return false;
  }
  EscPressRouted = true;
  return true;
}

ToggleAction NativeUiBridge::engineActivate() {
  if (!armed())
    return ToggleAction::kNone;
  return requestMenuUp(NativeUiOrigin::kBaseUi) ? ToggleAction::kOpened
                                                : ToggleAction::kNone;
}

ToggleAction NativeUiBridge::engineHide() {
  if (!armed() || !MenuUp)
    return ToggleAction::kNone;
  // The shipped CGameUI::HideGameUI only acts when the engine reports a
  // connection; without a level the home page stays put (T04) and the
  // toggle is consumed with no action - exactly the vanilla no-op. A load
  // in progress has no level either, so a direct hide does not cancel it
  // (T14 owns the cancel UX).
  if (!HasLevel)
    return ToggleAction::kNone;
  // One route per physical press: the wrapper's post dispatch repeats the
  // same engine call (IsPost=false in both phases, hook-contract T5) and a
  // held ESC would re-fire the window key handler; both must coalesce into
  // one action. The mark clears on the physical key-up or at the next
  // raise, so the composition case (press 1 cancels, release, press 2
  // closes) keeps its two-step semantics.
  if (EscPressRouted) {
    ++Counters.EscRepeatsSuppressed;
    return ToggleAction::kNone;
  }
  EscPressRouted = true;
  return PageShown ? ToggleAction::kRouteClose : ToggleAction::kStateClose;
}

NativeUiBridge &nativeUiBridge() {
  static NativeUiBridge bridge;
  return bridge;
}

void LoadingFeed::push(const LoadingFeedEvent &event) {
  if (Count == kCapacity) {
    // Drop the oldest: newest progress must survive a burst.
    Head = (Head + 1) % kCapacity;
    --Count;
    ++Dropped;
  }
  Events[(Head + Count) % kCapacity] = event; // copies the strings
  ++Count;
}

bool LoadingFeed::pop(LoadingFeedEvent &out) {
  if (Count == 0)
    return false;
  out = Events[Head];
  Head = (Head + 1) % kCapacity;
  --Count;
  return true;
}

LoadingFeed &loadingFeed() {
  static LoadingFeed feed;
  return feed;
}

LoadingStep classifyLoadingEvent(const LoadingFeedEvent &event) {
  switch (event.kind) {
  case LoadingFeedKind::kLoadingStarted:
    return LoadingStep::kStarted;
  case LoadingFeedKind::kLoadingFinished:
    // Only the level resource is the engine-confirmed join (the same fact
    // the vanilla hide gate reads); anything else is bookkeeping - never
    // assumed as "in game" (T14 step a.3).
    return event.text == "level" ? LoadingStep::kJoined : LoadingStep::kNone;
  case LoadingFeedKind::kStopProgress:
    // error=false is the clean per-session teardown (T02 evidence: a
    // StopProgressBar(error=0) precedes every new phase); only the error
    // branch is a failure outcome.
    return event.error ? LoadingStep::kFailed : LoadingStep::kNone;
  case LoadingFeedKind::kPasswordPrompt:
    return LoadingStep::kPasswordRequired;
  default:
    return LoadingStep::kNone; // progress/status values only
  }
}

} // namespace services
