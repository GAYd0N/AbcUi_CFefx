#include "UiInputRouter.h"

namespace framework {
namespace uiinput {

Token::Token(Token &&other) noexcept
    : Router_(other.Router_), Owner_(other.Owner_),
      Generation(other.Generation) {
  other.Router_ = nullptr;
  other.Owner_ = nullptr;
  other.Generation = 0;
}

Token &Token::operator=(Token &&other) noexcept {
  if (this != &other) {
    release();
    Router_ = other.Router_;
    Owner_ = other.Owner_;
    Generation = other.Generation;
    other.Router_ = nullptr;
    other.Owner_ = nullptr;
    other.Generation = 0;
  }
  return *this;
}

Token::~Token() {
  release();
}

bool Token::held() const {
  if (!Router_ || !Owner_)
    return false;
  for (size_t i = 0; i < Router_->Count; i++) {
    if (Router_->Stack[i].owner == Owner_ &&
        Router_->Stack[i].generation == Generation)
      return true;
  }
  return false;
}

void Token::release() {
  if (!Router_)
    return;
  Router_->releaseToken(*this);
  Router_ = nullptr;
  Owner_ = nullptr;
  Generation = 0;
}

Token::Token(Router *router, Owner *owner, uint32_t generation)
    : Router_(router), Owner_(owner), Generation(generation) {}

Router::Router(Sink *sink) : Sink_(sink) {}

bool Router::inRange(int keynum) const {
  return keynum >= 0 && keynum <= kMaxKeynum;
}

size_t Router::topIndex() const {
  // Highest tier wins; ties break to the most recent acquire (Stack is
  // append-ordered, so the LAST matching entry is the most recent).
  size_t top = 0;
  for (size_t i = 1; i < Count; i++) {
    if (Stack[i].tier >= Stack[top].tier)
      top = i;
  }
  return top;
}

Token Router::acquire(Owner *owner, OwnerTier tier) {
  if (!owner || Count == kMaxOwners)
    return Token();
  const bool wasEmpty = Count == 0;
  Stack[Count] = Entry{owner, tier, NextGeneration++};
  Count++;
  if (wasEmpty) {
    // Baseline: keys the binding chain already holds pass through for
    // clean pairing (V04: no stuck keys across the menu toggle); the UI
    // starts its owned session with no stale held keys.
    for (int k = 0; k <= kMaxKeynum; k++)
      PreHeld[k] = EngineDown[k];
    releaseUiHeldKeys();
    if (Sink_)
      Sink_->onExclusiveChange(true);
  }
  return Token(this, owner, Stack[Count - 1].generation);
}

void Router::removeAt(size_t index, bool notifySink) {
  for (size_t i = index + 1; i < Count; i++)
    Stack[i - 1] = Stack[i];
  Count--;
  // The departing session's UI-held keys die with it - pair them now, not
  // only when the stack empties (a menu closing under a nested owner must
  // not leave ghost held keys behind).
  releaseUiHeldKeys();
  if (Count == 0) {
    for (int k = 0; k <= kMaxKeynum; k++)
      PreHeld[k] = false;
    if (Sink_ && notifySink)
      Sink_->onExclusiveChange(false);
  }
}

void Router::notifyLost(const Entry &entry) {
  if (entry.owner)
    entry.owner->onOwnershipLost();
}

void Router::releaseToken(const Token &token) {
  if (!token.Owner_)
    return;
  for (size_t i = 0; i < Count; i++) {
    if (Stack[i].owner == token.Owner_ &&
        Stack[i].generation == token.Generation) {
      // Normal release: the owner initiated it (it already cleaned its
      // mirrored state), so no onOwnershipLost notification - that is
      // reserved for force-releases (focus loss / reset).
      removeAt(i, true);
      return;
    }
  }
  // Stale token (already force-released): a no-op by design.
}

void Router::releaseUiHeldKeys() {
  for (int k = 0; k <= kMaxKeynum; k++) {
    if (UiHeld[k]) {
      UiHeld[k] = false;
      if (Sink_)
        Sink_->uiKeyUp(k);
    }
  }
}

Route Router::onKey(int down, int keynum, bool isWheel) {
  Route route;
  const bool range = inRange(keynum);
  const bool wheel = range && isWheel;

  if (Count != 0) {
    const Entry &top = Stack[topIndex()];
    // Pre-held keys keep their engine edges (down repeats and the final
    // up) so the bindings pair exactly as if no menu had opened; they
    // are not re-fed to the UI (its state was cleaned at acquire).
    const bool preheld = range && !wheel && PreHeld[keynum];
    if (preheld) {
      EngineDown[keynum] = down != 0;
      if (!down)
        PreHeld[keynum] = false;
      return route;
    }
    if (!wheel && top.owner && top.owner->claimsKey(down, keynum)) {
      if (range && down) {
        // Auto-repeat downs keep feeding the UI (native caret/scroll
        // repeat) but the claimed-key action fires exactly once per
        // physical press; the owner may release its own token inside
        // (ESC), which pairs the key just fed.
        Sink_->uiKeyDown(keynum);
        route.uiKey = true;
        const bool first = !UiHeld[keynum];
        UiHeld[keynum] = true;
        if (first)
          top.owner->onClaimedKeyDown(keynum);
      } else if (range && !down && UiHeld[keynum]) {
        Sink_->uiKeyUp(keynum);
        UiHeld[keynum] = false;
        route.uiKey = true;
      }
      route.consumeEngine = true;
      return route;
    }
  }

  if (range && !wheel) {
    if (down) {
      EngineDown[keynum] = true;
      // Observe-first mirror (play behavior): the UI sees the edge
      // whether or not a menu is up - no owner, or the owner declined
      // the key (scoreboard pointer mode keeps chat keys live).
      Sink_->uiKeyDown(keynum);
      UiHeld[keynum] = true;
      route.uiKey = true;
    } else {
      EngineDown[keynum] = false;
      // Conditional up: a synth pairing up may already have cleared
      // this key when a session released - feeding a second up would
      // break the one-down-run-one-up guarantee.
      if (UiHeld[keynum]) {
        Sink_->uiKeyUp(keynum);
        UiHeld[keynum] = false;
        route.uiKey = true;
      }
    }
  }
  return route;
}

bool Router::wheelClaimed() const {
  if (Count == 0)
    return false;
  const Entry &top = Stack[topIndex()];
  return top.owner && top.owner->claimsWheel();
}

bool Router::onWheelPulse(int keynum) const {
  if (Count == 0)
    return false;
  const Entry &top = Stack[topIndex()];
  return top.owner && top.owner->onClaimedWheelPulse(keynum);
}

bool Router::blocksGameplay() const {
  if (Count == 0)
    return false;
  const Entry &top = Stack[topIndex()];
  return top.owner && top.owner->blocksGameplay();
}

bool Router::exclusive() const {
  return Count != 0;
}

void Router::drainOwners(bool notifySink) {
  // Pop before notifying: an owner that re-enters release() from its
  // mirrored-state cleanup must find its token already stale.
  while (Count != 0) {
    const Entry entry = Stack[topIndex()];
    removeAt(topIndex(), notifySink);
    notifyLost(entry);
  }
}

void Router::resetKeyState() {
  releaseUiHeldKeys();
  // Engine down-state is stale after focus loss / teardown (releases may
  // never have been observed); the next acquire re-baselines from the live
  // stream.
  for (int k = 0; k <= kMaxKeynum; k++) {
    EngineDown[k] = false;
    PreHeld[k] = false;
  }
}

void Router::onFocusLost() {
  drainOwners(true);
  resetKeyState();
}

void Router::reset() {
  // Teardown: the owners still clean up, but no engine-side effect may run -
  // not the sink's (see the header contract), and not the ones an owner
  // reaches from its force-release (tearingDown()).
  Teardown_ = true;
  drainOwners(false);
  resetKeyState();
  Teardown_ = false;
}

bool Router::tearingDown() const {
  return Teardown_;
}

size_t Router::ownerCount() const {
  return Count;
}

bool Router::ownedBy(const Owner *owner) const {
  for (size_t i = 0; i < Count; i++) {
    if (Stack[i].owner == owner)
      return true;
  }
  return false;
}

} // namespace uiinput
} // namespace framework
