#pragma once

// framework::uiinput - UI input ownership and keyboard routing (T06).
//
// ONE ownership truth: a tiered owner stack with RAII tokens replaces the
// old single UiMouseOwner bool that every window wrote directly. Whoever
// holds a token owns UI input; the top-of-stack owner (highest tier, then
// most recently acquired) decides what each key edge does. The router is
// deliberately free of engine, VGUI and Rml includes so the exact
// production state machine links into offline tests (05 doc evidence tier
// 1); the Rml feed and the engine side effects live behind Sink.
//
// Keyboard contract (the physical key stream enters ONCE, see
// rmlui::routeKey for the injection point):
// - No owner: observe-first passthrough (today's play behavior - every edge
//   reaches the engine/binding chain and is mirrored into the UI).
// - Exclusive: keys the top owner claims are swallowed from the bindings and
//   fed to the UI; keys the engine already held when the first token landed
//   ("pre-held": W/attack pressed before the menu opened) keep passing
//   through so the bindings pair cleanly - no stuck keys, no synthetic
//   engine releases. The consumption decision is the owner policy; Rml's
//   handled-flag is never read as a consume signal.
// - Keydown/keyup pairing is the router's: every edge fed to the UI is
//   tracked, and a UI-held key gets a synthetic UI keyup when its menu
//   closes, a sibling releases, focus is lost or the router resets. Engine
//   keyups are never fabricated.
// - onClaimedKeyDown fires exactly once per physical press (auto-repeat
//   downs keep feeding the UI - native caret/scroll repeat - but never
//   re-fire the action; ESC executes its state action once).

#include <cstddef>
#include <cstdint>

namespace framework {
namespace uiinput {

// Tier order. A capture owner (key-binding capture, T11) sits above menu
// pages; existing interactive HUD pointer modes sit below everything new.
enum class OwnerTier {
  kInteractiveHud = 0,
  kMenu = 1,
  kCapture = 2,
};

class Owner {
public:
  virtual ~Owner() = default;

  // Consulted while this owner is top-of-stack: true = swallow the edge
  // from the engine/binding chain and feed it to the UI. Pre-held keys
  // never reach here (the router passes their edges through for pairing).
  virtual bool claimsKey(int down, int keynum) = 0;
  // Wheel pulses scroll the UI instead of switching weapons while true.
  virtual bool claimsWheel() = 0;
  // While true the game must not act on the engine's own input state for
  // this frame (a text field owns the keyboard). Swallowing key edges is
  // not enough: the engine's bind handler runs before this hook sees the
  // edge, so +/-forward can already be latched when the first character is
  // typed. The client CreateMove seam zeroes the movement axes while this
  // reports true, which is how the native chat entry keeps the player
  // standing still.
  virtual bool blocksGameplay() const {
    return false;
  }
  // Exactly-once action per physical press of a claimed key (post
  // auto-repeat dedup). Called after the edge was fed to the UI; the owner
  // may release its own token inside (ESC closing the menu).
  virtual void onClaimedKeyDown([[maybe_unused]] int keynum) {}
  // T11 capture seam: a claimed wheel pulse is offered to the top owner
  // BEFORE the scroll claim applies. True = the pulse was consumed (key
  // capture); false = the usual scroll path runs.
  virtual bool onClaimedWheelPulse([[maybe_unused]] int keynum) {
    return false;
  }
  // Force-release notification (focus loss, reset, mid-stack removal) so
  // the owner drops its mirrored state instead of keeping a second truth.
  // The token is already stale when this runs.
  virtual void onOwnershipLost() {}
};

// RAII ownership token. Movable, not copyable; release() is idempotent and
// generation-checked - a token whose slot was force-released is a no-op.
class Token {
public:
  Token() = default;
  Token(Token &&other) noexcept;
  Token &operator=(Token &&other) noexcept;
  ~Token();

  bool held() const;
  void release();

private:
  friend class Router;
  Token(class Router *router, Owner *owner, uint32_t generation);

  Router *Router_ = nullptr;
  Owner *Owner_ = nullptr;
  uint32_t Generation = 0;
};

// The Rml feed seam (production: rmlui::InputBridge; tests: a recorder).
// The router invokes it directly - the pairing bookkeeping and the feeds
// live in one place, so a synthetic up can never be forgotten.
class Sink {
public:
  virtual ~Sink() = default;

  virtual void uiKeyDown(int keynum) = 0;
  virtual void uiKeyUp(int keynum) = 0;
  // Stack empty<->non-empty transitions: the engine mouse mode and cursor
  // side effects hang here so nested owners never fight over them.
  virtual void onExclusiveChange(bool exclusive) = 0;
};

// One key-edge decision. uiKey mirrors what was (or will be) fed to the UI
// sink; wheel claims are a separate query (wheelClaimed) because the engine
// delivers wheel as down-only pulses with no pairing semantics.
struct Route {
  bool uiKey = false;         // the edge was fed to the UI sink
  bool consumeEngine = false; // true = return 0, bindings never see it
};

class Router {
public:
  static constexpr size_t kMaxOwners = 8;
  static constexpr int kMaxKeynum = 255;

  explicit Router(Sink *sink);
  // Nested ownership: menus over HUD pointer modes over nothing. Fails
  // (returns an empty token) past kMaxOwners - unrepresentable depth, like
  // the MenuState FixedStack precedent.
  Token acquire(Owner *owner, OwnerTier tier);

  // The single keyboard injection point. Down/up edges arrive as the
  // engine key stream delivers them (auto-repeat included). `isWheel`
  // marks the wheel-pulse keynums (down-only, no pairing) - the caller
  // knows them from keydefs.h; the router stays engine-header-free.
  Route onKey(int down, int keynum, bool isWheel);

  // Wheel pulses scroll the UI only when the top owner claims them.
  bool wheelClaimed() const;
  // True while the top owner blocks the engine's input state (see
  // Owner::blocksGameplay): the CreateMove clamp reads this.
  bool blocksGameplay() const;
  // T11 capture seam: true when the top owner consumed the pulse (capture
  // took the wheel edge; the scroll path must not run).
  bool onWheelPulse(int keynum) const;
  bool exclusive() const;

  // Window focus lost: synthetic UI keyups for everything held, every
  // token force-released (owners notified top-down), engine down-state
  // cleared as stale. Idempotent (the surface WndProc dispatches twice).
  void onFocusLost();
  // HUD_Shutdown/ExitGame teardown: same ownership cleanup as focus loss
  // (tokens force-released, owners notified, held keys paired), but no
  // engine-side effect is delivered - neither the sink's exclusive-change
  // notification nor an engine seam an owner runs from its force-release.
  // Teardown runs inside HUD_Shutdown, which the engine reaches only after
  // it unloaded the VGUI modules those effects call into
  // (vgui2ext::lockCursor -> ISurface::CalculateMouseVisible -> the engine
  // CBaseUI over a freed vgui2 IPanel; the menu-down console seam ->
  // IBaseUI::HideConsole -> the released engine console object): running
  // them there faults the exit instead of restoring a cursor that no longer
  // exists. Engine-facing seams read tearingDown().
  void reset();
  // True for the whole reset() teardown drain (owners notified, none of
  // their engine-side seams may run - see reset()).
  bool tearingDown() const;

  // Inspection (tests and diagnostics).
  size_t ownerCount() const;
  bool ownedBy(const Owner *owner) const;

private:
  // Token release/held walk the stack directly (generation-checked).
  friend class Token;

  struct Entry {
    Owner *owner = nullptr;
    OwnerTier tier = OwnerTier::kInteractiveHud;
    uint32_t generation = 0;
  };

  bool inRange(int keynum) const;
  // Top-of-stack: highest tier wins, ties break to the most recent acquire.
  size_t topIndex() const;
  void releaseToken(const Token &token);
  // Synthetic UI keyups for every tracked held key (pairing guarantee).
  void releaseUiHeldKeys();
  // Held-key pairing plus the stale engine down-state clear.
  void resetKeyState();
  // Force-release every owner entry top-down, notifying each owner;
  // notifySink announces the empty transition to the sink (side effects).
  void drainOwners(bool notifySink);
  void removeAt(size_t index, bool notifySink);
  void notifyLost(const Entry &entry);

  Sink *Sink_;
  Entry Stack[kMaxOwners] = {};
  size_t Count = 0;
  uint32_t NextGeneration = 1;
  bool Teardown_ = false;               // reset() drain in progress
  bool EngineDown[kMaxKeynum + 1] = {}; // keys the binding chain holds
  bool UiHeld[kMaxKeynum + 1] = {};     // edges fed to the UI, up pending
  bool PreHeld[kMaxKeynum + 1] = {};    // EngineDown snapshot at first acquire
};

} // namespace uiinput
} // namespace framework
