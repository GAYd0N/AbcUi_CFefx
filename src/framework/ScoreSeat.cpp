// framework::scoreseat implementation - see ScoreSeat.h. Ported from the
// built-in scoreboard's interaction half (old CScorePanel's pointer mode and
// the Viewport's +/-showscores / intermission seams).
#include "ScoreSeat.h"

#include "../modsys/EventBus.h"
#include "../rmlui/InputBridge.h"
#include "../rmlui/VGUI2ExtensionImport.h"

#include <abcui/events.h>

#include <keydefs.h>

namespace framework {
namespace scoreseat {
namespace {

constexpr int kMouse2 = K_MOUSE2;
// Old scoreboard right-click: MOUSE1..MOUSE5 and the wheel belong to the
// pointer while it is up.
constexpr int kFirstMouseKey = K_MOUSE1;
constexpr int kLastMouseKey = K_MOUSE5;

bool Intermission = false;
bool Held = false;
bool Visible = false;
bool MouseMode = false;
uiinput::Token PointerToken;

void publishPointerState(bool enabled) {
  const AbcUiBoolEvent event{enabled ? 1 : 0};
  framework::eventBus().publish(ABCUI_CHANNEL_HUD_SCORE_POINTER, event);
}

class SeatOwner final : public uiinput::Owner {
public:
  bool claimsKey([[maybe_unused]] int down, int keynum) override {
    return MouseMode &&
           ((keynum >= kFirstMouseKey && keynum <= kLastMouseKey) ||
            keynum == K_MWHEELUP || keynum == K_MWHEELDOWN);
  }
  bool claimsWheel() override {
    return MouseMode;
  }
  void onOwnershipLost() override {
    // The token is already stale; drop the mirror without re-releasing.
    MouseMode = false;
    publishPointerState(false);
  }
};

SeatOwner Owner;

} // namespace

void setIntermission(bool intermission) {
  // Edge-triggered: HUD_Redraw calls this every frame with the current flag;
  // only the transitions drive the module (old SetInterMission was a state
  // change, not a per-frame vote).
  if (intermission == Intermission)
    return;
  Intermission = intermission;
  if (intermission)
    setPointerMode(false);
  const AbcUiBoolEvent event{intermission ? 1 : 0};
  framework::eventBus().publish(ABCUI_CHANNEL_HUD_INTERMISSION, event);
}

bool intermission() {
  return Intermission;
}

void setHeld(bool held) {
  Held = held;
}

bool held() {
  return Held;
}

void setVisible(bool visible) {
  Visible = visible;
  if (!visible)
    setPointerMode(false);
}

bool visible() {
  return Visible;
}

void setPointerMode(bool enabled) {
  enabled = enabled && Visible && !Intermission;
  if (enabled == MouseMode)
    return;
  MouseMode = enabled;
  // Ownership gates both Rml input and the client mouse/view hooks; the
  // native cursor lifetime follows the router's first/last owner edge.
  if (enabled) {
    PointerToken =
        rmlui::uiRouter().acquire(&Owner, uiinput::OwnerTier::kInteractiveHud);
    vgui2ext::centerAndUnlockCursor();
  } else {
    PointerToken.release();
  }
  publishPointerState(enabled);
}

bool pointerMode() {
  return MouseMode;
}

bool wantsWheel() {
  return Visible && MouseMode;
}

bool onKeyEvent(int down, int keynum) {
  if (!Visible)
    return false;
  if (keynum == kMouse2 && down && !MouseMode) {
    // Enter once; subsequent right presses belong to the row context menu.
    setPointerMode(true);
    return true;
  }
  return MouseMode && ((keynum >= kFirstMouseKey && keynum <= kLastMouseKey) ||
                       keynum == K_MWHEELUP || keynum == K_MWHEELDOWN);
}

} // namespace scoreseat
} // namespace framework
