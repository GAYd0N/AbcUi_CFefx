// abcui.damage - the damage feedback family: damage-type tiles, the
// directional pain indicator, floating damage numbers and the grenade
// proximity marker. Moved from src/panels/{dmgtiles,indicator,popnum,
// gindicator} (old CDmgTilesPanel/CIndicatorPanel/CPopNumPanel/
// CGIndicatorPanel; ABCEnchance sources src/hud/vgui/{dmgtiles,indicator,
// popnum,gindicator}).
//
// All four panels hang off the same two feeds: netmsg.damage (tiles +
// direction), netmsg.popnum (addon carrier numbers) and hud.addentity
// (grenade tracking; the module tracks entities by index, never by the
// engine-owned pointer).
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <cstring>
#include <string>
#include <vector>
#include <string_view>

namespace {

const AbcUiModuleApi *Api = nullptr;

constexpr const char *kTilesPanel = "dmgtiles";
constexpr const char *kIndicatorPanel = "indicator";
constexpr const char *kPopNumPanel = "popnum";
constexpr const char *kGrenadePanel = "gindicator";

AbcUiPanelDesc TilesDesc = {};
AbcUiPanelDesc IndicatorDesc = {};
AbcUiPanelDesc PopNumDesc = {};
AbcUiPanelDesc GrenadeDesc = {};

constexpr float kFrontDotGate = 0.5f;

// ------------------------------------------------------------- dmgtiles ----

// Old HudAnimations.txt DMGTiles_* sequence: alpha 0 -> 255 in 0.1s, hold,
// fade to 0 in 0.1s. D-17 #4: the hold now honors the res "keep_time 5"
// entry the old code shipped but never consumed.
constexpr double kTileFadeIn = 0.1;
constexpr double kTileHold = 5.0;
constexpr double kTileFadeOut = 0.1;

// Old DMG_TYPE bits per tile, ctor order (Poison..Sonic). The enum values are
// the wire contract (UpdateTiles bits) - never renumber.
constexpr int32_t kTileBits[10] = {
    1 << 17,              // poison  DMG_POISON
    1 << 20,              // acid    DMG_ACID
    (1 << 4) | (1 << 22), // freeze  DMG_FREEZE | DMG_SLOWFREEZE
    1 << 14,              // drown   DMG_DROWN
    (1 << 3) | (1 << 21), // burn    DMG_BURN | DMG_SLOWBURN
    1 << 16,              // gas     DMG_NERVEGAS
    1 << 18,              // radiation DMG_RADIATION
    (1 << 8) | (1 << 27), // shock   DMG_SHOCK | DMG_SHOCK_GLOW
    1 << 10,              // energybeam DMG_ENERGYBEAM
    1 << 9,               // sonic   DMG_SONIC
};
constexpr const char *kTileIds[10] = {
    "tile-poison", "tile-acid",      "tile-freeze", "tile-drown", "tile-burn",
    "tile-gas",    "tile-radiation", "tile-shock",  "tile-beam",  "tile-sonic",
};
constexpr const char *kLabelIds[10] = {
    "label-poison", "label-acid",  "label-freeze",    "label-drown",
    "label-burn",   "label-gas",   "label-radiation", "label-shock",
    "label-beam",   "label-sonic",
};

struct TileState {
  double showTime = -1.0;
  bool visible = false;
};
TileState Tiles[10];
double TilesTime = 0.0;

// ------------------------------------------------------------ indicator ----

// Old CIndicatorPanel ctor defaults (persisted contract - never rename).
// hud_damageshock* were created-but-unused in the old ctor as well (the
// damage-shock feature predated the IndicatorImage rework) - kept for cvar
// surface parity, no consumer.
constexpr const char *kIndicatorStyle = "hud_indicator_style";
constexpr const char *kDamageShock = "hud_damageshock";
constexpr const char *kDamageShockFactor = "hud_damageshock_factor";
constexpr const char *kDamageShockBase = "hud_damageshock_base";

// Old IndicatorPanel.res timing.
constexpr double kIndicatorKeepTime = 2.0;
constexpr double kIndicatorFadeTime = 1.0;

// Old IndicatorStyle enum: Hide=0, GoldSrc=1 (quantized quadrants), New=2.
constexpr int kStyleHide = 0;
constexpr int kStyleGoldSrc = 1;
constexpr int kStyleNew = 2;

// Slot rotor element ids (old IndicatorImage1..4). The per-slot offsets
// (30/40/50/50) are baked into the RCSS slot positions.
constexpr const char *kRotorIds[4] = {"rotor-1", "rotor-2", "rotor-3",
                                      "rotor-4"};
constexpr int kIndicatorSlots = 4;

AbcUiCvarHandle IndicatorStyle = nullptr;
AbcUiCvarHandle DamageShock = nullptr;
AbcUiCvarHandle DamageShockFactor = nullptr;
AbcUiCvarHandle DamageShockBase = nullptr;

struct IndicatorSlot {
  double startTime = -1.0;
  float hitFrom[3] = {};
  std::string appliedAngle;
};
IndicatorSlot IndicatorSlots[kIndicatorSlots];
int NextIndicatorSlot = 0;
double IndicatorTime = 0.0;

// --------------------------------------------------------------- popnum ----

// Old CViewport ctor default (persisted contract - never rename).
constexpr const char *kPopNumber = "hud_popnumber";
constexpr int kThousand = 1000;
constexpr int kMillion = 1000000;
constexpr int kHundredMillion = 100000000;
// D-17 #5: fixed lifetime (the old 2-per-frame alpha at the 60fps
// reference: 255 / 2 / 60 seconds), identical at any framerate.
constexpr float kNumberLifetime = 255.0f / 2.0f / 60.0f;
constexpr float kPopBoxWidth = 48.0f;  // old PopNumberPanel.res wide
constexpr float kPopBoxHeight = 64.0f; // old PopNumberPanel.res tall

AbcUiCvarHandle PopNumber = nullptr;

struct PopNumberItem {
  float origin[3] = {};
  double createTime = 0.0;
  float alpha = 255.0f;
  AbcUiElement *element = nullptr;
};
std::vector<PopNumberItem> PopNumbers;
double PopNumTime = 0.0;

// ------------------------------------------------------------ gindicator ----

// Old CGenadeIndicatorPanel ctor defaults (persisted contract - never rename).
constexpr const char *kGrenadeIndicator = "hud_indicator_grenade";
constexpr const char *kGrenadeRange = "hud_indicator_grenade_range";
constexpr const char *kGrenadeTime = "hud_indicator_grenade_time";
constexpr const char *kGrenadeModelPath = "models/w_grenade.mdl";

AbcUiCvarHandle GrenadeIndicator = nullptr;
AbcUiCvarHandle GrenadeRange = nullptr;
AbcUiCvarHandle GrenadeTime = nullptr;

// Marker geometry in raw HUD px (the same basis as the other readouts).
constexpr float kGrenadeIconSize = 24.0f;
constexpr float kGrenadeArrowSize = 14.0f;
constexpr float kGrenadeEdgeInset = 3.0f;
constexpr float kGrenadeIconGap = 4.0f;

struct TrackedGrenade {
  int index = 0;
  float origin[3] = {};
  float expire = 0.0f;
  int32_t messageNum = 0;
  bool noDraw = false;
  AbcUiElement *marker = nullptr; // wrapper box (icon + arrow)
  AbcUiElement *arrow = nullptr;
};
std::vector<TrackedGrenade> Tracked;
double GrenadeTimeNow = 0.0;

// --------------------------------------------------------------- helpers ----

AbcUiDocument *panelDocument(const char *name) {
  return Api->ui->panelDocument(name);
}

void setElementProperty(const char *panel, const char *id, const char *name,
                        const char *value) {
  AbcUiDocument *doc = panelDocument(panel);
  if (!doc)
    return;
  if (AbcUiElement *element = Api->ui->querySelector(doc, id))
    Api->ui->elementSetProperty(element, name, value);
}

void setVisible(const char *panel, bool visible) {
  AbcUiDocument *doc = panelDocument(panel);
  if (!doc)
    return;
  if (visible)
    Api->ui->documentShow(doc);
  else
    Api->ui->documentHide(doc);
}

bool documentVisible(const char *panel) {
  AbcUiDocument *doc = panelDocument(panel);
  return doc && Api->ui->documentVisible(doc);
}

int cvarInt(AbcUiCvarHandle cvar, int fallback) {
  return cvar ? static_cast<int>(
                    Api->console->cvarValue(cvar, static_cast<float>(fallback)))
              : fallback;
}

float cvarFloat(AbcUiCvarHandle cvar, float fallback) {
  return cvar ? Api->console->cvarValue(cvar, fallback) : fallback;
}

void localPlayer(AbcUiPlayerState &state) {
  state = AbcUiPlayerState{};
  state.structSize = sizeof(state);
  Api->data->playerState(&state);
}

void hudState(AbcUiHudState &state) {
  state = AbcUiHudState{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
}

// Old engine AngleVectors inverse (abc::math::VectorAngles): pitch/yaw from a
// direction vector, roll left at 0.
void vectorAngles(const float forward[3], float angles[3]) {
  const float flat =
      std::sqrt(forward[0] * forward[0] + forward[1] * forward[1]);
  if (flat <= 0.00001f) {
    angles[0] = forward[2] > 0.0f ? -90.0f : 90.0f;
    angles[1] = 0.0f;
    angles[2] = 0.0f;
    return;
  }
  angles[0] = std::atan2(-forward[2], flat) * 180.0f / 3.14159265358979323846f;
  angles[1] =
      std::atan2(forward[1], forward[0]) * 180.0f / 3.14159265358979323846f;
  angles[2] = 0.0f;
}

// View forward vector for the front-facing gates (AngleVectors).
void viewForward(const AbcUiPlayerState &player, float forward[3]) {
  const float pitch = player.angles[0] * 3.14159265358979323846f / 180.0f;
  const float yaw = player.angles[1] * 3.14159265358979323846f / 180.0f;
  forward[0] = std::cos(pitch) * std::cos(yaw);
  forward[1] = std::cos(pitch) * std::sin(yaw);
  forward[2] = -std::sin(pitch);
}

// Engine AngleVectors basis with roll 0: the view right/up axes the edge
// clamp resolves a direction against (also for points behind the eye).
void viewBasis(const AbcUiPlayerState &player, float right[3], float up[3]) {
  const float pitch = player.angles[0] * 3.14159265358979323846f / 180.0f;
  const float yaw = player.angles[1] * 3.14159265358979323846f / 180.0f;
  const float sp = std::sin(pitch), cp = std::cos(pitch);
  const float sy = std::sin(yaw), cy = std::cos(yaw);
  right[0] = sy;
  right[1] = -cy;
  right[2] = 0.0f;
  up[0] = sp * cy;
  up[1] = sp * sy;
  up[2] = cp;
}

bool inFront(const float origin[3], const AbcUiPlayerState &player,
             const float forward[3]) {
  if (player.index == 0)
    return false;
  const float rel[3] = {origin[0] - player.origin[0],
                        origin[1] - player.origin[1],
                        origin[2] - player.origin[2]};
  const float length =
      std::sqrt(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
  if (length <= 0.0f)
    return false;
  const float dot =
      (rel[0] * forward[0] + rel[1] * forward[1] + rel[2] * forward[2]) /
      length;
  return dot > kFrontDotGate;
}

std::string translate(const char *token) {
  char out[512];
  return Api->ui->translate(token, out, sizeof(out)) ? std::string(out)
                                                     : std::string();
}

// ------------------------------------------------------------ dmgtiles ----

void updateTiles(int32_t bitsDamage) {
  for (int i = 0; i < 10; i++) {
    if ((kTileBits[i] & bitsDamage) == 0)
      continue;
    // Old CTileIconItem::Show: visible + restart the animation sequence.
    Tiles[i].showTime = TilesTime;
    Tiles[i].visible = true;
    if (!documentVisible(kTilesPanel))
      setVisible(kTilesPanel, true);
  }
}

void tilesDocumentReady(void *) {
  // Old labels were vgui-localized tokens ("#abcui.damage/DMGTiles_Poison");
  // resolve them once here (raw token fallback = the old missing-entry
  // behavior).
  AbcUiDocument *doc = panelDocument(kTilesPanel);
  if (!doc)
    return;
  for (int i = 0; i < 10; i++) {
    AbcUiElement *label = Api->ui->querySelector(doc, kLabelIds[i]);
    if (!label)
      continue;
    char buffer[256];
    if (Api->ui->elementInnerRML(label, buffer, sizeof(buffer)) <= 0)
      continue;
    const std::string text = translate(buffer);
    if (!text.empty())
      Api->ui->elementSetInnerRML(label, text.c_str());
  }
}

void tilesOnData(void *, double frameTime) {
  TilesTime += frameTime;
  AbcUiHudState state{};
  hudState(state);
  // Old Viewport-wide visibility (kHideAll token hides everything).
  if (state.hideBits & ABCUI_HIDEBIT_ALL)
    setVisible(kTilesPanel, false);
  else if (state.suit)
    setVisible(kTilesPanel, true);

  for (int i = 0; i < 10; i++) {
    TileState &tile = Tiles[i];
    if (!tile.visible)
      continue;
    const double elapsed = TilesTime - tile.showTime;
    std::string opacity;
    if (elapsed < kTileFadeIn) {
      // Ramp up (old Linear 0.0 -> 0.1 to 255).
      opacity = std::format("{:g}", elapsed / kTileFadeIn);
      setElementProperty(kTilesPanel, kTileIds[i], "display", "block");
      setElementProperty(kTilesPanel, kTileIds[i], "opacity", opacity.c_str());
    } else if (elapsed < kTileFadeIn + kTileHold) {
      setElementProperty(kTilesPanel, kTileIds[i], "display", "block");
      setElementProperty(kTilesPanel, kTileIds[i], "opacity", "1");
    } else if (elapsed < kTileFadeIn + kTileHold + kTileFadeOut) {
      opacity = std::format("{:g}", 1.0 - (elapsed - kTileFadeIn - kTileHold) /
                                              kTileFadeOut);
      setElementProperty(kTilesPanel, kTileIds[i], "display", "block");
      setElementProperty(kTilesPanel, kTileIds[i], "opacity", opacity.c_str());
    } else {
      // Old sequence end: the tile stays invisible until the next Show.
      tile.visible = false;
      setElementProperty(kTilesPanel, kTileIds[i], "display", "none");
    }
  }
}

void tilesReset(void *) {
  // Old Reset: hide the panel + all tiles.
  setVisible(kTilesPanel, false);
  for (TileState &tile : Tiles) {
    tile.showTime = -1.0;
    tile.visible = false;
  }
  for (const char *id : kTileIds)
    setElementProperty(kTilesPanel, id, "display", "none");
}

// ------------------------------------------------------------ indicator ----

void onHit(const float from[3]) {
  // Old SetHitIndicator: claim the next cycling slot, restart its clock.
  IndicatorSlot &slot = IndicatorSlots[NextIndicatorSlot];
  for (int i = 0; i < 3; i++)
    slot.hitFrom[i] = from[i];
  slot.startTime = IndicatorTime;
  NextIndicatorSlot = (NextIndicatorSlot + 1) % kIndicatorSlots;
  if (!documentVisible(kIndicatorPanel))
    setVisible(kIndicatorPanel, true);
}

void indicatorOnData(void *, double frameTime) {
  IndicatorTime += frameTime;
  AbcUiHudState state{};
  hudState(state);
  if (state.hideBits & ABCUI_HIDEBIT_ALL) {
    setVisible(kIndicatorPanel, false);
    return;
  }

  const int style = cvarInt(IndicatorStyle, kStyleNew);
  bool anyActive = false;
  for (int i = 0; i < kIndicatorSlots; i++) {
    IndicatorSlot &slot = IndicatorSlots[i];
    if (slot.startTime < 0.0)
      continue;

    const float elapsed = static_cast<float>(IndicatorTime - slot.startTime);
    if (elapsed >= kIndicatorKeepTime + kIndicatorFadeTime) {
      // Old OnThink completion path: hide + reset the clock.
      slot.startTime = -1.0;
      slot.appliedAngle.clear();
      setElementProperty(kIndicatorPanel, kRotorIds[i], "display", "none");
      continue;
    }

    AbcUiPlayerState player{};
    localPlayer(player);
    if (player.index == 0)
      continue;
    if (style <= kStyleHide) {
      // Old IndicatorImage::Paint hid every draw under the style gate; the
      // rotors vanish with it.
      setElementProperty(kIndicatorPanel, kRotorIds[i], "display", "none");
      continue;
    }

    // Relative yaw of the hit source vs the local player's entity yaw (old
    // IndicatorImage::Paint math, verbatim).
    float rel[3] = {slot.hitFrom[0] - player.origin[0],
                    slot.hitFrom[1] - player.origin[1],
                    slot.hitFrom[2] - player.origin[2]};
    vectorAngles(rel, rel);
    rel[1] -= player.angles[1]; // yaw
    float degAngle = std::fmod(rel[1], 360.0f);
    if (degAngle < 0.0f)
      degAngle += 360.0f;
    if (style == kStyleGoldSrc) {
      // Old GoldSrc quadrant quantization.
      if ((degAngle >= 0.0f && degAngle <= 45.0f) ||
          (degAngle >= 315.0f && degAngle <= 360.0f))
        degAngle = 0.0f;
      else if (degAngle > 45.0f && degAngle <= 135.0f)
        degAngle = 90.0f;
      else if (degAngle > 135.0f && degAngle <= 225.0f)
        degAngle = 180.0f;
      else
        degAngle = 270.0f;
    }

    const std::string transform = std::format("rotate({:.1f}deg)", degAngle);
    if (slot.appliedAngle != transform) {
      slot.appliedAngle = transform;
      setElementProperty(kIndicatorPanel, kRotorIds[i], "transform",
                         transform.c_str());
    }

    // Old alpha ramp: hold 255 for keep_time, linear fade over fade_time.
    const float alpha =
        elapsed <= kIndicatorKeepTime
            ? 1.0f
            : 1.0f - (elapsed - kIndicatorKeepTime) / kIndicatorFadeTime;
    const std::string opacity = std::format("{:g}", alpha);
    setElementProperty(kIndicatorPanel, kRotorIds[i], "display", "block");
    setElementProperty(kIndicatorPanel, kRotorIds[i], "opacity",
                       opacity.c_str());
    anyActive = true;
  }
  if (!anyActive)
    setVisible(kIndicatorPanel, false);
}

void indicatorReset(void *) {
  // Old Reset: clear slots + hide all rotors.
  for (IndicatorSlot &slot : IndicatorSlots) {
    slot.startTime = -1.0;
    slot.appliedAngle.clear();
  }
  NextIndicatorSlot = 0;
  setVisible(kIndicatorPanel, false);
  for (const char *id : kRotorIds)
    setElementProperty(kIndicatorPanel, id, "display", "none");
}

// --------------------------------------------------------------- popnum ----

// Old ctor: divide first, then localize with the matching suffix token.
std::string formatPopValue(int value) {
  std::string token = "PopNumber_O";
  if (value > kHundredMillion) {
    value /= kHundredMillion;
    token = "PopNumber_B";
  } else if (value > kMillion) {
    value /= kMillion;
    token = "PopNumber_M";
  } else if (value > kThousand) {
    value /= kThousand;
    token = "PopNumber_K";
  }
  std::string text = translate(token.c_str());
  if (text.empty())
    text = "{0}"; // bare number (the old null-Find case rendered nothing;
                  // D-17 keeps the value visible)
  // D-17: the tokens carry {0} placeholders and substitute here - no printf
  // near the table text.
  const std::string key = "{0}";
  const std::string number = std::to_string(value);
  size_t at = 0;
  while ((at = text.find(key, at)) != std::string::npos) {
    text.replace(at, key.size(), number);
    at += number.size();
  }
  return text;
}

void addPopNumber(const float origin[3], int value, const int color[4]) {
  AbcUiDocument *doc = panelDocument(kPopNumPanel);
  if (!doc)
    return;
  AbcUiElement *container = Api->ui->querySelector(doc, "numbers");
  if (!container)
    return;
  if (PopNumbers.size() >= 128) {
    if (PopNumbers.front().element)
      Api->ui->elementRemove(PopNumbers.front().element);
    PopNumbers.erase(PopNumbers.begin());
  }
  const std::string markup = formatPopValue(value);
  AbcUiElement *number =
      Api->ui->elementAppendRml(container, markup.c_str(), "number");
  if (!number)
    return;
  Api->ui->elementSetClass(number, "font-default", 1);
  char tint[64];
  Api->ui->formatRgba(color[0], color[1], color[2], color[3], tint,
                      sizeof(tint));
  Api->ui->elementSetProperty(number, "color", tint);

  PopNumberItem entry;
  for (int i = 0; i < 3; i++)
    entry.origin[i] = origin[i];
  entry.createTime = PopNumTime;
  entry.element = number;
  PopNumbers.push_back(entry);
  if (!documentVisible(kPopNumPanel))
    setVisible(kPopNumPanel, true);
}

void popNumReset(void *) {
  // Old per-panel Destory (map change Reset sweeps every pop number).
  for (PopNumberItem &number : PopNumbers) {
    if (number.element)
      Api->ui->elementRemove(number.element);
  }
  PopNumbers.clear();
  setVisible(kPopNumPanel, false);
}

void popNumOnRedraw(void *, double frameTime) {
  PopNumTime += frameTime;
  AbcUiDocument *doc = panelDocument(kPopNumPanel);
  if (!doc)
    return;

  // Old Viewport-wide visibility (kHideAll token hides everything).
  AbcUiHudState hud{};
  hudState(hud);
  if (hud.hideBits & ABCUI_HIDEBIT_ALL)
    setVisible(kPopNumPanel, false);
  else if (!PopNumbers.empty())
    setVisible(kPopNumPanel, true);

  // Old OnThink per number: front-facing gate re-checked for the
  // reposition, bob y = proj.y - tall * (-sin(age)/2).
  const float density = Api->ui->scale();
  const float boxWidth = kPopBoxWidth * density;
  const float boxHeight = kPopBoxHeight * density;
  AbcUiPlayerState player{};
  localPlayer(player);
  float forward[3] = {};
  viewForward(player, forward);

  for (size_t i = 0; i < PopNumbers.size();) {
    PopNumberItem &number = PopNumbers[i];
    // D-17 #5: the old label alpha decayed a fixed 2 per frame - a
    // framerate-dependent lifetime. Time-based now, referenced to the old
    // 60fps behavior.
    number.alpha =
        255.0f * std::max(0.0f, 1.0f - static_cast<float>(PopNumTime -
                                                          number.createTime) /
                                           kNumberLifetime);
    if (number.alpha <= 0.0f) {
      if (number.element)
        Api->ui->elementRemove(number.element);
      PopNumbers.erase(PopNumbers.begin() + i);
      continue;
    }

    if (inFront(number.origin, player, forward)) {
      float screen[2] = {};
      if (Api->ui->project(number.origin, screen) != 0) {
        if (number.element)
          Api->ui->elementSetProperty(number.element, "display", "none");
        ++i;
        continue;
      }
      const float age = static_cast<float>(PopNumTime - number.createTime);
      const float ratio = -std::sin(age) / 2.0f;
      if (number.element) {
        const std::string left =
            std::format("{:g}px", screen[0] - boxWidth / 2.0f);
        const std::string top =
            std::format("{:g}px", screen[1] - boxHeight * ratio);
        const std::string opacity = std::format("{:g}", number.alpha / 255.0f);
        Api->ui->elementSetProperty(number.element, "display", "block");
        Api->ui->elementSetProperty(number.element, "left", left.c_str());
        Api->ui->elementSetProperty(number.element, "top", top.c_str());
        Api->ui->elementSetProperty(number.element, "opacity", opacity.c_str());
      }
    } else if (number.element) {
      Api->ui->elementSetProperty(number.element, "display", "none");
    }
    ++i;
  }
  if (PopNumbers.empty())
    setVisible(kPopNumPanel, false);
}

// ------------------------------------------------------------ gindicator ----

void onAddEntity(const AbcUiAddEntityEvent &event) {
  if (cvarInt(GrenadeIndicator, 1) <= 0)
    return;
  // D-17: the old gate also required curstate.sequence != 0 - on SC 5.26 the
  // grenade throw entities carry sequence 0 for their whole flight, so the
  // old code never drew a marker. The sequence gate is dropped; model name /
  // studio type / messagenum / range gates stay.
  if (event.modelType != ABCUI_MODEL_STUDIO ||
      std::string_view(event.model) != kGrenadeModelPath)
    return;
  AbcUiPlayerState player{};
  localPlayer(player);
  if (player.index == 0 || event.messageNum != player.messageNum)
    return;
  const float rel[3] = {player.origin[0] - event.origin[0],
                        player.origin[1] - event.origin[1],
                        player.origin[2] - event.origin[2]};
  const float length =
      std::sqrt(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
  if (length > cvarFloat(GrenadeRange, 256.0f))
    return;
  for (TrackedGrenade &tracked : Tracked) {
    if (tracked.index != event.index)
      continue;
    // Refresh the live fields; the expiry clock keeps its first-seen value
    // (old code updated the pointer only).
    for (int i = 0; i < 3; i++)
      tracked.origin[i] = event.origin[i];
    tracked.messageNum = event.messageNum;
    tracked.noDraw = (event.effects & ABCUI_EF_NODRAW) != 0;
    return;
  }
  TrackedGrenade tracked;
  tracked.index = event.index;
  for (int i = 0; i < 3; i++)
    tracked.origin[i] = event.origin[i];
  tracked.expire =
      static_cast<float>(GrenadeTimeNow) + cvarFloat(GrenadeTime, 4.0f);
  tracked.messageNum = event.messageNum;
  tracked.noDraw = (event.effects & ABCUI_EF_NODRAW) != 0;
  Tracked.push_back(tracked);
}

// Marker box: the old 24x24 grenade icon, plus a direction triangle that
// only appears while the marker is pinned to the screen edge.
void setPx(AbcUiElement *element, const char *name, float value) {
  const std::string text = std::format("{:g}px", value);
  Api->ui->elementSetProperty(element, name, text.c_str());
}

void hideGrenadeMarker(TrackedGrenade &tracked) {
  if (tracked.marker)
    Api->ui->elementSetProperty(tracked.marker, "display", "none");
}

void grenadeOnRedraw(void *, double frameTime) {
  GrenadeTimeNow += frameTime;
  AbcUiDocument *doc = panelDocument(kGrenadePanel);
  if (!doc)
    return;

  // Old Viewport-wide visibility (kHideAll token hides everything).
  AbcUiHudState hud{};
  hudState(hud);
  if (hud.hideBits & ABCUI_HIDEBIT_ALL)
    setVisible(kGrenadePanel, false);
  else
    setVisible(kGrenadePanel, true);

  AbcUiPlayerState player{};
  localPlayer(player);

  // Old OnThink expiry sweep runs regardless of the paint gates.
  for (size_t i = 0; i < Tracked.size();) {
    TrackedGrenade &tracked = Tracked[i];
    const bool expired = cvarInt(GrenadeIndicator, 1) <= 0 ||
                         player.index == 0 || tracked.noDraw ||
                         tracked.messageNum != player.messageNum ||
                         tracked.expire <= static_cast<float>(GrenadeTimeNow);
    if (expired) {
      if (tracked.marker)
        Api->ui->elementRemove(tracked.marker);
      Tracked.erase(Tracked.begin() + i);
      continue;
    }
    i++;
  }

  if (player.index == 0)
    return;
  float right[3] = {};
  float up[3] = {};
  viewBasis(player, right, up);

  AbcUiElement *container = Api->ui->querySelector(doc, "markers");
  if (!container)
    return;

  int screenW = 0, screenH = 0;
  if (!Api->ui->documentSize(doc, &screenW, &screenH) || screenW <= 0 ||
      screenH <= 0)
    return;
  const float width = static_cast<float>(screenW);
  const float height = static_cast<float>(screenH);
  const float centerX = width * 0.5f;
  const float centerY = height * 0.5f;
  // The icon box stays fully visible; the arrow rides the edge inset.
  const float iconMargin = kGrenadeIconSize * 0.5f + 2.0f;
  const float iconMinX = iconMargin, iconMaxX = width - iconMargin;
  const float iconMinY = iconMargin, iconMaxY = height - iconMargin;
  const float edgeMinX = kGrenadeEdgeInset + kGrenadeArrowSize * 0.5f;
  const float edgeMaxX = width - edgeMinX;
  const float edgeMinY = kGrenadeEdgeInset + kGrenadeArrowSize * 0.5f;
  const float edgeMaxY = height - edgeMinY;
  // Icon-center distance from the arrow center (half arrow + gap + half icon).
  const float reticleOffset =
      kGrenadeArrowSize * 0.5f + kGrenadeIconGap + kGrenadeIconSize * 0.5f;
  const float range = cvarFloat(GrenadeRange, 256.0f);

  for (TrackedGrenade &tracked : Tracked) {
    if (!tracked.marker) {
      tracked.marker = Api->ui->elementAppendRml(
          container,
          "<img class=\"icon\" "
          "src=\"mod://abcui.damage/hud/img/grenadeicon.png\"/>"
          "<img class=\"arrow\" "
          "src=\"mod://abcui.damage/hud/img/grenadearrow.png\"/>",
          "grenade");
      tracked.arrow = Api->ui->elementChild(tracked.marker, 1);
    }
    if (!tracked.marker)
      continue;
    const float rel[3] = {tracked.origin[0] - player.origin[0],
                          tracked.origin[1] - player.origin[1],
                          tracked.origin[2] - player.origin[2]};
    const float length =
        std::sqrt(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
    if (length > range || length <= 0.0f) {
      hideGrenadeMarker(tracked);
      continue;
    }

    // Project first: the engine reads nonzero for a point behind the eye.
    // Behind or outside the viewport no longer hides the marker - the old
    // dot>0.5 front gate made a grenade in the same VIS group vanish the
    // moment it left the view, which left the player without a direction.
    float projected[2] = {};
    const bool behind = Api->ui->project(tracked.origin, projected) != 0;
    const bool onScreen = !behind && projected[0] >= iconMinX &&
                          projected[0] <= iconMaxX &&
                          projected[1] >= iconMinY && projected[1] <= iconMaxY;

    float iconX = projected[0];
    float iconY = projected[1];
    float dirX = 0.0f, dirY = 0.0f;
    bool clamped = !onScreen;
    if (clamped) {
      if (!behind) {
        dirX = projected[0] - centerX;
        dirY = projected[1] - centerY;
      } else {
        // View-space basis (engine AngleVectors, roll 0): right/up components
        // give the direction in every quadrant, including directly behind
        // the eye where the engine projection mirrors.
        const float xr =
            rel[0] * right[0] + rel[1] * right[1] + rel[2] * right[2];
        const float yu = rel[0] * up[0] + rel[1] * up[1] + rel[2] * up[2];
        dirX = xr;
        dirY = -yu;
      }
      const float magnitude = std::sqrt(dirX * dirX + dirY * dirY);
      if (magnitude < 0.0001f) {
        // Exactly behind/above: point the arrow at the bottom edge.
        dirX = 0.0f;
        dirY = 1.0f;
      } else {
        dirX /= magnitude;
        dirY /= magnitude;
      }
      // Cast the direction ray from the screen centre to the edge rect.
      const float tX = dirX > 0.0f   ? (edgeMaxX - centerX) / dirX
                       : dirX < 0.0f ? (edgeMinX - centerX) / dirX
                                     : 1.0e30f;
      const float tY = dirY > 0.0f   ? (edgeMaxY - centerY) / dirY
                       : dirY < 0.0f ? (edgeMinY - centerY) / dirY
                                     : 1.0e30f;
      const float t = (std::min)(tX, tY);
      const float edgeX = std::clamp(centerX + dirX * t, edgeMinX, edgeMaxX);
      const float edgeY = std::clamp(centerY + dirY * t, edgeMinY, edgeMaxY);
      // Keep the icon inside the screen, just inboard of the arrow.
      iconX = std::clamp(edgeX - dirX * reticleOffset, iconMinX, iconMaxX);
      iconY = std::clamp(edgeY - dirY * reticleOffset, iconMinY, iconMaxY);
      if (tracked.arrow) {
        // Arrow centre relative to the (24x24) marker box's top-left.
        setPx(tracked.arrow, "left",
              edgeX - (iconX - kGrenadeIconSize * 0.5f) -
                  kGrenadeArrowSize * 0.5f);
        setPx(tracked.arrow, "top",
              edgeY - (iconY - kGrenadeIconSize * 0.5f) -
                  kGrenadeArrowSize * 0.5f);
        const float degrees =
            std::atan2(dirY, dirX) * 180.0f / 3.14159265358979323846f;
        const std::string rotation = std::format("rotate({:g}deg)", degrees);
        Api->ui->elementSetProperty(tracked.arrow, "transform",
                                    rotation.c_str());
        Api->ui->elementSetProperty(tracked.arrow, "display", "block");
      }
    } else if (tracked.arrow) {
      Api->ui->elementSetProperty(tracked.arrow, "display", "none");
    }

    // The marker box is the 24x24 icon box; the old panel centred it on the
    // projection (left = x - 12).
    Api->ui->elementSetProperty(tracked.marker, "display", "block");
    setPx(tracked.marker, "left", iconX - kGrenadeIconSize * 0.5f);
    setPx(tracked.marker, "top", iconY - kGrenadeIconSize * 0.5f);
  }
}

void grenadeReset(void *) {
  // Old Reset: ShowPanel(true) + clear the list (the map change invalidates
  // every tracked entity anyway).
  for (TrackedGrenade &tracked : Tracked) {
    if (tracked.marker)
      Api->ui->elementRemove(tracked.marker);
  }
  Tracked.clear();
  setVisible(kGrenadePanel, true);
}

// ---- event sinks ----

void onDamage(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiDamageEvent *>(payload);
  updateTiles(event->tiles);
  onHit(event->from);
}

void onPopNum(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiPopNumEvent *>(payload);
  // Old Viewport POPNUMBER handler: cvar gate + front-facing check at spawn,
  // then AddPopNumber.
  if (cvarInt(PopNumber, 1) <= 0)
    return;
  AbcUiPlayerState player{};
  localPlayer(player);
  float forward[3] = {};
  viewForward(player, forward);
  if (!inFront(event->origin, player, forward))
    return;
  addPopNumber(event->origin, event->value, event->color);
}

void onAddEntityEvent(void *, const void *payload) {
  onAddEntity(*static_cast<const AbcUiAddEntityEvent *>(payload));
}

// ---- lifecycle ----

void registerPanel(AbcUiPanelDesc &desc, const char *name, const char *doc,
                   void (*ready)(void *), void (*onData)(void *, double),
                   void (*onRedraw)(void *, double), void (*reset)(void *)) {
  desc.structSize = sizeof(desc);
  desc.name = name;
  desc.documentPath = doc;
  desc.zIndex = 0;
  desc.userdata = nullptr;
  desc.onDocumentReady = ready;
  desc.onData = onData;
  desc.onRedraw = onRedraw;
  desc.reset = reset;
  Api->panels->registerPanel(&desc);
}

void onModuleLoad() {
  registerPanel(TilesDesc, kTilesPanel,
                "mod://abcui.damage/hud/rml/dmgtiles.rml", &tilesDocumentReady,
                &tilesOnData, nullptr, &tilesReset);
  registerPanel(IndicatorDesc, kIndicatorPanel,
                "mod://abcui.damage/hud/rml/indicator.rml", nullptr, nullptr,
                &indicatorOnData, &indicatorReset);
  registerPanel(PopNumDesc, kPopNumPanel,
                "mod://abcui.damage/hud/rml/popnum.rml", nullptr, nullptr,
                &popNumOnRedraw, &popNumReset);
  registerPanel(GrenadeDesc, kGrenadePanel,
                "mod://abcui.damage/hud/rml/gindicator.rml", nullptr, nullptr,
                &grenadeOnRedraw, &grenadeReset);

  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_DAMAGE, 0, &onDamage, nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_POP_NUM, 0, &onPopNum, nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_ADD_ENTITY, 0, &onAddEntityEvent,
                         nullptr);
}

void onModuleInit() {
  // damage family cvars (persisted contract - never rename).
  PopNumber = Api->console->registerCvar(kPopNumber, "1", ABCUI_CVAR_VALUE);
  IndicatorStyle =
      Api->console->registerCvar(kIndicatorStyle, "2", ABCUI_CVAR_VALUE);
  DamageShock = Api->console->registerCvar(kDamageShock, "1", ABCUI_CVAR_VALUE);
  DamageShockFactor =
      Api->console->registerCvar(kDamageShockFactor, "0.015", ABCUI_CVAR_VALUE);
  DamageShockBase =
      Api->console->registerCvar(kDamageShockBase, "15", ABCUI_CVAR_VALUE);
  GrenadeIndicator =
      Api->console->registerCvar(kGrenadeIndicator, "1", ABCUI_CVAR_VALUE);
  GrenadeRange =
      Api->console->registerCvar(kGrenadeRange, "256", ABCUI_CVAR_VALUE);
  GrenadeTime = Api->console->registerCvar(kGrenadeTime, "4", ABCUI_CVAR_VALUE);
}

void onModuleShutdown() {
  // The documents die with the context; every element handle must go.
  Tracked.clear();
  PopNumbers.clear();
  for (IndicatorSlot &slot : IndicatorSlots) {
    slot.startTime = -1.0;
    slot.appliedAngle.clear();
  }
  for (TileState &tile : Tiles) {
    tile.showTime = -1.0;
    tile.visible = false;
  }
}

void onModuleUnload() {
  Api = nullptr;
}

} // namespace

ABCUI_MODULE_EXPORT int ABCUI_CALL
AbcUiModule_Main(uint32_t abiVersion, const AbcUiModuleApi *api,
                 AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api ||
      !AbcUiModule_InitDescriptor(out))
    return 0;
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->panels ||
      !api->events || !api->ui || !api->console || !api->data)
    return 0;
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->console->structSize < sizeof(AbcUiConsoleApi) ||
      api->data->structSize < sizeof(AbcUiDataApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.damage";
  out->version = "2.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
