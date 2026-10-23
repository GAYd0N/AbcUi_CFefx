// abcui.cfefx - cumulative damage / kill-streak HUD feedback, moved from
// src/panels/cfefx (old CCfefxPanel; ABCEnchance sources
// src/hud/vgui/cfefx.*).
//
// Old control mapping: one VGUI CCfefxPanel with nine damage-mark ImagePanels,
// nine star ImagePanels, a score mark and a score effect becomes one RmlUi
// document. The VGUI AnimationController sequences in hudanimations.txt are
// reproduced with timestamps in onData (the DmgTiles-era instrument), and the
// old 854x480 absolute animation coordinates are re-anchored to the live
// viewport so the effect follows the HUD density scale on any aspect ratio.
//
// State: every 1/10 of hud_cfefx_max damage queues one star, so a single big
// hit plays its whole queue (8000 damage at the default max launches eighty
// stars, ten per full accumulator) instead of skipping the animation. The nine
// star sprites are reused in order and a slot only takes a new launch once its
// previous star has landed, so no star animation is cut; the side marks are
// allowed to be interrupted while the queue keeps flowing. The score pop does
// not wait for the queue: as soon as the accumulator reaches a full
// hud_cfefx_max (1000 damage by default) it shows immediately, and its image
// follows the damage reached (1000 shows NanoDamege1, 5000 shows NanoDamege5).
// The old combo counter is kept and lifts the image further, so consecutive
// pops still climb the ladder; a score within hud_cfefx_time of the previous
// one advances the combo, otherwise it starts again at 1. Each side mark pops
// in opaque the moment its own star lands, matching the CF original.
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

const AbcUiModuleApi *Api = nullptr;

constexpr const char *kPanelName = "cfefx";
constexpr const char *kDocumentPath = "mod://abcui.cfefx/hud/rml/cfefx.rml";
// HUD panels share the default document tier (0) across the modules; paint
// order inside the tier falls back to module load order, so the feedback never
// sits above another HUD document (the old VGUI panel's zpos 998/999 has no
// equivalent in the module catalogs).

// Document element ids, one per old vgui control.
constexpr const char *kMarkIds[9] = {
    "dmg-mark-one",   "dmg-mark-two",   "dmg-mark-three",
    "dmg-mark-four",  "dmg-mark-five",  "dmg-mark-six",
    "dmg-mark-seven", "dmg-mark-eight", "dmg-mark-nine",
};
constexpr const char *kStarIds[9] = {
    "star-one", "star-two",   "star-three", "star-four", "star-five",
    "star-six", "star-seven", "star-eight", "star-nine",
};

// Old hudanimations.txt ypos c-98 / c-74 / c-50 / c-26 and DmgMarkFive c-122,
// expressed as absolute design-space top edges (480-line base).
constexpr float kPipTop[9] = {142.0f, 166.0f, 190.0f, 214.0f, 118.0f,
                              142.0f, 166.0f, 190.0f, 214.0f};

// Package-relative, exactly like the document's own src attributes: RmlUi
// joins a src against the document directory, so "../img/cfefx/..." resolves
// inside the module package.
constexpr const char *kScoreImages[10] = {
    "../img/cfefx/NanoDamege1.png", "../img/cfefx/NanoDamege2.png",
    "../img/cfefx/NanoDamege3.png", "../img/cfefx/NanoDamege4.png",
    "../img/cfefx/NanoDamege5.png", "../img/cfefx/NanoDamege6.png",
    "../img/cfefx/NanoDamege7.png", "../img/cfefx/NanoDamege8.png",
    "../img/cfefx/NanoDamege9.png", "../img/cfefx/NanoDamege10.png",
};

// Engine sound path (relative to the mod's sound/ directory).
constexpr const char *kScoreSound = "abc/cfefx/UI_SPECIALKILL2.wav";

// Geometry from the old CfefxPanel.res / hudanimations.txt, in design dp.
constexpr float kRightMargin = 44.0f;
constexpr float kStarSize = 20.0f;
constexpr float kStarBigSize = 40.0f;
constexpr float kStarMidFromRight = 204.0f;
constexpr float kScoreWidth = 96.0f;
constexpr float kScoreHeight = 80.0f;
constexpr float kScoreCenterOffset = -48.0f;
constexpr float kScoreTop = 340.0f;
constexpr float kScoreEffectWidth = 40.0f;
constexpr float kScoreEffectHeight = 35.0f;
constexpr float kScoreEffectRight = 44.0f;
constexpr float kScoreEffectTop = 80.0f;
// Old ScoreEffect drawColor was white with alpha 200.
constexpr float kScoreEffectBaseAlpha = 200.0f / 255.0f;

// Old hudanimations timings. The stars are queued: each new action starts
// kStarStagger after the previous one, so a single 400-damage hit launches its
// four stars as a continuous trail instead of four synchronized animations.
// The side mark appears the moment its own star lands (no fade).
constexpr double kStarStagger = 0.025;
constexpr double kStarDuration = 0.3;
constexpr double kScoreMarkEnd = 2.0;
constexpr double kScoreEffectEnd = 1.2;

// Old ctor defaults (persisted contract - never rename).
constexpr const char *kEnabledCvar = "hud_cfefx";
constexpr const char *kMaxDamageCvar = "hud_cfefx_max";
constexpr const char *kVolumeCvar = "hud_cfefx_volume";
constexpr const char *kKillTimeCvar = "hud_cfefx_time";

AbcUiCvarHandle Enabled = nullptr;
AbcUiCvarHandle MaxDamage = nullptr;
AbcUiCvarHandle Volume = nullptr;
AbcUiCvarHandle KillTime = nullptr;

struct Pip {
  AbcUiElement *mark = nullptr;
  AbcUiElement *star = nullptr;
  double starStart = -1.0;
};

AbcUiPanelDesc PanelDesc = {};

double ClientTimeNow = 0.0;
// Stars waiting for a launch slot; the frame pump launches them one at a time,
// on the earliest free pip, 0.025 s apart. A big hit only raises this counter,
// so every tenth of its damage gets its own visible star instead of the later
// schedule overwriting an earlier one.
std::size_t PendingStars = 0;
double NextStarStart = 0.0;
std::uint64_t Damage = 0;
std::size_t LastTier = 0;
std::size_t Score = 0;
double LastScoreTime = 0.0;
double ScoreStart = -1.0;

Pip Pips[9];
AbcUiElement *ScoreMark = nullptr;
AbcUiElement *ScoreEffect = nullptr;
// Tracks the last hudActive() result so reset() runs on the transition into
// the inactive state instead of rebuilding an already-clean panel per frame.
bool HudActive = false;

// --------------------------------------------------------------- helpers ----

float lerp(float from, float to, float t) { return from + (to - from) * t; }

// Normalized 0..1 progress inside [start, start + duration].
float segment(double time, double start, double duration) {
  if (duration <= 0.0)
    return time >= start ? 1.0f : 0.0f;
  return std::clamp(static_cast<float>((time - start) / duration), 0.0f, 1.0f);
}

AbcUiDocument *panelDocument() { return Api->ui->panelDocument(kPanelName); }

float cvarFloat(AbcUiCvarHandle cvar, float fallback) {
  return cvar ? Api->console->cvarValue(cvar, fallback) : fallback;
}

// hud_cfefx_max / 10, floored at 10 like the old CREATE_CVAR callback.
std::uint64_t damageStep() {
  const float maxDamage = std::max(10.0f, cvarFloat(MaxDamage, 1000.0f));
  return static_cast<std::uint64_t>(maxDamage) / 10;
}

bool hudActive() {
  if (!panelDocument())
    return false;
  if (cvarFloat(Enabled, 0.0f) <= 0.0f)
    return false;

  AbcUiPlayerState player{};
  player.structSize = sizeof(player);
  Api->data->playerState(&player);
  if (player.index == 0)
    return false;

  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  if (state.hideBits & (ABCUI_HIDEBIT_ALL | ABCUI_HIDEBIT_WEAPONS))
    return false;
  return state.health > 0;
}

bool screenMetrics(AbcUiDocument *doc, float &width, float &height,
                   float &scale) {
  int pixelsWide = 0;
  int pixelsTall = 0;
  if (!doc || !Api->ui->documentSize(doc, &pixelsWide, &pixelsTall) ||
      pixelsWide <= 0 || pixelsTall <= 0)
    return false;
  width = static_cast<float>(pixelsWide);
  height = static_cast<float>(pixelsTall);
  scale = Api->ui->scale();
  return true;
}

void setOpacity(AbcUiElement *element, float opacity) {
  if (!element)
    return;
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.4f",
                std::clamp(opacity, 0.0f, 1.0f));
  Api->ui->elementSetProperty(element, "opacity", buffer);
}

void setBox(AbcUiElement *element, float left, float top, float width,
            float height) {
  if (!element)
    return;
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), "%.2fpx", left);
  Api->ui->elementSetProperty(element, "left", buffer);
  std::snprintf(buffer, sizeof(buffer), "%.2fpx", top);
  Api->ui->elementSetProperty(element, "top", buffer);
  std::snprintf(buffer, sizeof(buffer), "%.2fpx", width);
  Api->ui->elementSetProperty(element, "width", buffer);
  std::snprintf(buffer, sizeof(buffer), "%.2fpx", height);
  Api->ui->elementSetProperty(element, "height", buffer);
}

void showPanel() {
  if (AbcUiDocument *doc = panelDocument()) {
    if (!Api->ui->documentVisible(doc))
      Api->ui->documentShow(doc);
  }
}

void hidePanel() {
  if (AbcUiDocument *doc = panelDocument())
    Api->ui->documentHide(doc);
}

// ----------------------------------------------------------- panel state ----

void reset();
void evaluate();
void fireScorePop();

void playScoreSound() {
  const float volume = cvarFloat(Volume, 0.25f);
  if (volume <= 0.0f)
    return;
  Api->console->playSound(kScoreSound, volume);
}

void updateScoreEffect(std::size_t image) {
  ScoreStart = ClientTimeNow;
  if (ScoreMark)
    Api->ui->elementSetAttribute(
        ScoreMark, "src", kScoreImages[std::min<std::size_t>(image, 9)]);
}

void resetScoreEffect() {
  ScoreStart = -1.0;
  setOpacity(ScoreMark, 0.0f);
  setOpacity(ScoreEffect, 0.0f);
  if (ScoreMark)
    Api->ui->elementSetAttribute(ScoreMark, "src", kScoreImages[0]);
}

void resetDmgMark(std::size_t index) {
  if (index >= 9)
    return;
  Pip &pip = Pips[index];
  pip.starStart = -1.0;
  setOpacity(pip.mark, 0.0f);
  setOpacity(pip.star, 0.0f);

  float width = 0.0f, height = 0.0f, scale = 1.0f;
  if (pip.star && screenMetrics(panelDocument(), width, height, scale)) {
    const float half = kStarSize * 0.5f * scale;
    setBox(pip.star, width * 0.5f - half, height * 0.5f - half,
           kStarSize * scale, kStarSize * scale);
  }
}

void resetAllMarks() {
  for (std::size_t i = 0; i < 9; i++)
    resetDmgMark(i);
}

void hideSideMark(std::size_t index) {
  if (index >= 9)
    return;
  setOpacity(Pips[index].mark, 0.0f);
}

// The first pip whose previous star has landed (or which never flew), so the
// nine sprites are recycled without cutting an animation mid-flight.
std::size_t freePipSlot() {
  for (std::size_t i = 0; i < 9; i++) {
    const Pip &pip = Pips[i];
    if (!pip.mark || !pip.star)
      continue;
    if (pip.starStart < 0.0 || ClientTimeNow >= pip.starStart + kStarDuration)
      return i;
  }
  return 9;
}

void launchStar(std::size_t index) {
  Pip &pip = Pips[index];
  hideSideMark(index);
  pip.starStart = ClientTimeNow;
  NextStarStart = ClientTimeNow + kStarStagger;
}

// Launches at most one pending star per frame: the 0.025 s cadence plus the
// nine-sprite pool (each star lives 0.3 s) paces a long queue at roughly thirty
// stars a second, so a big hit visibly plays every one of them in order.
void pumpStarQueue() {
  if (PendingStars == 0 || ClientTimeNow < NextStarStart)
    return;
  const std::size_t slot = freePipSlot();
  if (slot >= 9)
    return;
  launchStar(slot);
  PendingStars--;
}

void evaluate() {
  if (!hudActive()) {
    if (HudActive)
      reset();
    return;
  }
  HudActive = true;

  showPanel();

  const std::uint64_t step = damageStep();
  if (step == 0 || Damage < step)
    return;

  const std::size_t tier = static_cast<std::size_t>(Damage / step);
  if (tier <= LastTier)
    return;
  const std::size_t previous = LastTier;
  LastTier = tier;

  // Every tenth plays its own star: a single big hit piles the whole count into
  // the pending queue (5000 damage at the default max plays fifty stars, ten
  // per full accumulator) and the frame pump launches them in order.
  PendingStars += tier - previous;
  pumpStarQueue();

  // The pop does not wait for the star queue: a full accumulator shows it
  // immediately, and its image follows the damage reached.
  if (tier >= 10)
    fireScorePop();
}

void fireScorePop() {
  const std::uint64_t step = damageStep();
  const std::size_t tier =
      step == 0 ? 0 : static_cast<std::size_t>(Damage / step);
  // The image follows the damage reached: 1000 damage at the default max
  // (tier 10) shows NanoDamege1, 2000 shows NanoDamege2, 5000 NanoDamege5.
  const std::size_t tierImage =
      std::min<std::size_t>(9, std::max<std::size_t>(1, tier / 10) - 1);

  // Old combo window: reset only when the next score arrives after the
  // timeout - the score image itself is transient either way.
  if (Score != 0 && ClientTimeNow - LastScoreTime > cvarFloat(KillTime, 8.0f))
    Score = 0;

  // The combo counter still raises the image, so consecutive pops climb the
  // ladder even when the damage alone would show a smaller number.
  const std::size_t image =
      std::min<std::size_t>(9, std::max(tierImage, Score));

  playScoreSound();
  updateScoreEffect(image);
  LastScoreTime = ClientTimeNow;
  if (Score < 9)
    Score++;

  // Only the accumulator restarts; the stars already queued for this damage
  // keep playing, so nothing is cancelled by the pop.
  Damage = 0;
  LastTier = 0;
}

void onDamage(int value) {
  if (cvarFloat(Enabled, 1.0f) <= 0.0f)
    return;
  // Old AddDmg only accumulated positive numbers; heals still ran the
  // visibility/tier evaluation.
  if (value > 0)
    Damage += static_cast<std::uint64_t>(value);
  evaluate();
}

void reset() {
  HudActive = false;
  Damage = 0;
  LastTier = 0;
  Score = 0;
  LastScoreTime = 0.0;
  ScoreStart = -1.0;
  PendingStars = 0;
  NextStarStart = ClientTimeNow;

  resetAllMarks();
  resetScoreEffect();
  hidePanel();
}

// ------------------------------------------------------------ animation ----

void animatePip(Pip &pip, std::size_t index, double now) {
  if (!pip.mark || !pip.star)
    return;

  float width = 0.0f, height = 0.0f, scale = 1.0f;
  if (!screenMetrics(panelDocument(), width, height, scale))
    return;

  if (pip.starStart < 0.0) {
    setOpacity(pip.star, 0.0f);
    setOpacity(pip.mark, 0.0f);
    return;
  }

  // Queued but not launched yet.
  if (now < pip.starStart) {
    setOpacity(pip.star, 0.0f);
    setOpacity(pip.mark, 0.0f);
    return;
  }

  const double elapsed = now - pip.starStart;
  if (elapsed >= kStarDuration) {
    // The star has landed: the side mark appears in the same frame and is
    // fully opaque - the original has no fade on this transition.
    setOpacity(pip.star, 0.0f);
    setOpacity(pip.mark, 1.0f);
    return;
  }

  // A side mark is only shown once its own star has landed.
  setOpacity(pip.mark, 0.0f);

  // Old StarXAnim: centre -> right 204dp (20 -> 40) -> right 44dp (-> 20),
  // fading in over 0.02s and out over the last 0.1s. The old script held the
  // peak for 0.1s; keep a short 0.05s beat there and split the remaining
  // 0.25s evenly between the two legs.
  const float startX = width * 0.5f - kStarSize * 0.5f * scale;
  const float startY = height * 0.5f - kStarSize * 0.5f * scale;
  const float midX = width - kStarMidFromRight * scale;
  const float endX = width - kRightMargin * scale;
  const float top = kPipTop[index] * scale;
  const float smallSize = kStarSize * scale;
  const float bigSize = kStarBigSize * scale;

  float x = startX;
  float y = startY;
  float size = smallSize;
  if (elapsed < 0.125) {
    const float t = segment(elapsed, 0.0, 0.125);
    x = lerp(startX, midX, t);
    y = lerp(startY, top, t);
    size = lerp(smallSize, bigSize, t);
  } else if (elapsed < 0.175) {
    x = midX;
    y = top;
    size = bigSize;
  } else {
    const float t = segment(elapsed, 0.175, 0.125);
    x = lerp(midX, endX, t);
    y = top;
    size = lerp(bigSize, smallSize, t);
  }

  float alpha = 1.0f;
  if (elapsed < 0.02)
    alpha = static_cast<float>(elapsed / 0.02);
  else if (elapsed >= 0.2)
    alpha = 1.0f - static_cast<float>((elapsed - 0.2) / 0.1);

  setBox(pip.star, x, y, size, size);
  setOpacity(pip.star, std::clamp(alpha, 0.0f, 1.0f));
}

void animateScore(double now) {
  if (ScoreStart < 0.0)
    return;

  float width = 0.0f, height = 0.0f, scale = 1.0f;
  if (!screenMetrics(panelDocument(), width, height, scale))
    return;

  const double elapsed = now - ScoreStart;
  const float baseX = width * 0.5f + kScoreCenterOffset * scale;
  const float baseY = kScoreTop * scale;
  const float baseW = kScoreWidth * scale;
  const float baseH = kScoreHeight * scale;
  (void)height; // score geometry is height-relative through the HUD scale

  // Old ScoreMarkAnim: pop in from 12x10, settle at 96x80, four small
  // jitters, then fade from 1.5s to 2.0s.
  if (elapsed < kScoreMarkEnd) {
    float x = baseX;
    float y = baseY;
    float w = baseW;
    float h = baseH;

    if (elapsed < 0.06) {
      const float t = segment(elapsed, 0.0, 0.06);
      x = lerp(baseX + 42.0f * scale, baseX - 36.0f * scale, t);
      y = lerp(baseY + 35.0f * scale, baseY - 30.0f * scale, t);
      w = lerp(12.0f * scale, 168.0f * scale, t);
      h = lerp(10.0f * scale, 140.0f * scale, t);
    } else if (elapsed < 0.09) {
      const float t = segment(elapsed, 0.06, 0.03);
      x = lerp(baseX - 36.0f * scale, baseX, t);
      y = lerp(baseY - 30.0f * scale, baseY, t);
      w = lerp(168.0f * scale, baseW, t);
      h = lerp(140.0f * scale, baseH, t);
    } else if (elapsed < 0.11) {
      const float t = segment(elapsed, 0.09, 0.02);
      x = lerp(baseX, baseX + 3.0f * scale, t);
      y = lerp(baseY, baseY - 3.0f * scale, t);
    } else if (elapsed < 0.13) {
      const float t = segment(elapsed, 0.11, 0.02);
      x = lerp(baseX + 3.0f * scale, baseX - 3.0f * scale, t);
      y = lerp(baseY - 3.0f * scale, baseY + 3.0f * scale, t);
    } else if (elapsed < 0.15) {
      const float t = segment(elapsed, 0.13, 0.02);
      x = lerp(baseX - 3.0f * scale, baseX + 2.0f * scale, t);
      y = lerp(baseY + 3.0f * scale, baseY + 2.0f * scale, t);
    } else if (elapsed < 0.17) {
      const float t = segment(elapsed, 0.15, 0.02);
      x = lerp(baseX + 2.0f * scale, baseX, t);
      y = lerp(baseY + 2.0f * scale, baseY, t);
    }

    const float alpha = 1.0f - segment(elapsed, 1.5, 0.5);
    setBox(ScoreMark, x, y, w, h);
    setOpacity(ScoreMark, alpha);
  } else {
    setOpacity(ScoreMark, 0.0f);
  }

  // Old UpdateScoreEffect: fade in 0.2s, fly to ScoreMark * 1.05 from 0.2s
  // over 0.5s, grow to the effect's own size * 1.2 from 0.7s over 0.2s, fade
  // out from 0.9s over 0.3s.
  if (elapsed < kScoreEffectEnd) {
    const float startX = width - (kScoreEffectRight + kScoreEffectWidth) * scale;
    const float startY = kScoreEffectTop * scale;
    const float targetX = baseX * 1.05f;
    const float targetY = baseY * 1.05f;
    // Old UpdateScoreEffect scaled m_vecScoreEffectSize (the effect's own
    // 40x35), not the score mark size.
    const float targetW = kScoreEffectWidth * scale * 1.2f;
    const float targetH = kScoreEffectHeight * scale * 1.2f;

    float x = startX;
    float y = startY;
    float w = kScoreEffectWidth * scale;
    float h = kScoreEffectHeight * scale;
    float alpha = 0.0f;

    if (elapsed < 0.2) {
      alpha = segment(elapsed, 0.0, 0.2);
    } else if (elapsed < 0.7) {
      alpha = 1.0f;
      const float t = segment(elapsed, 0.2, 0.5);
      x = lerp(startX, targetX, t);
      y = lerp(startY, targetY, t);
    } else if (elapsed < 0.9) {
      alpha = 1.0f;
      x = targetX;
      y = targetY;
      const float t = segment(elapsed, 0.7, 0.2);
      w = lerp(kScoreEffectWidth * scale, targetW, t);
      h = lerp(kScoreEffectHeight * scale, targetH, t);
    } else {
      alpha = 1.0f - segment(elapsed, 0.9, 0.3);
      x = targetX;
      y = targetY;
      w = targetW;
      h = targetH;
    }

    setBox(ScoreEffect, x, y, w, h);
    setOpacity(ScoreEffect, alpha * kScoreEffectBaseAlpha);
  } else {
    setOpacity(ScoreEffect, 0.0f);
  }

  // The score mark is the longest-lived piece (fade ends at 2.0s); only then
  // is the whole score animation retired.
  if (elapsed >= kScoreMarkEnd)
    ScoreStart = -1.0;
}

// ------------------------------------------------------- panel callbacks ----

void onDocumentReady(void *) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;

  ScoreMark = Api->ui->querySelector(doc, "score-mark");
  ScoreEffect = Api->ui->querySelector(doc, "score-effect");
  for (std::size_t i = 0; i < 9; i++) {
    Pips[i].mark = Api->ui->querySelector(doc, kMarkIds[i]);
    Pips[i].star = Api->ui->querySelector(doc, kStarIds[i]);
  }

  reset();
}

void onData(void *, double frameTime) {
  ClientTimeNow += frameTime;

  if (!hudActive()) {
    if (HudActive)
      reset();
    return;
  }
  HudActive = true;
  showPanel();

  pumpStarQueue();
  animateScore(ClientTimeNow);
  for (std::size_t i = 0; i < 9; i++)
    animatePip(Pips[i], i, ClientTimeNow);
}

void onReset(void *) { reset(); }

void onPopNum(void *, const void *payload) {
  if (!payload)
    return;
  const auto *event = static_cast<const AbcUiPopNumEvent *>(payload);
  onDamage(event->value);
}

// -------------------------------------------------------------- cvars ------

void clampMaxDamage(void *) {
  if (!MaxDamage)
    return;
  if (Api->console->cvarValue(MaxDamage, 10.0f) < 10.0f)
    Api->console->setCvarValue(MaxDamage, 10.0f);
}

void clampVolume(void *) {
  if (!Volume)
    return;
  const float value = Api->console->cvarValue(Volume, 0.25f);
  if (value < 0.0f)
    Api->console->setCvarValue(Volume, 0.0f);
  else if (value > 1.0f)
    Api->console->setCvarValue(Volume, 1.0f);
}

void clampKillTime(void *) {
  if (!KillTime)
    return;
  if (Api->console->cvarValue(KillTime, 8.0f) < 0.0f)
    Api->console->setCvarValue(KillTime, 0.0f);
}

#ifndef NDEBUG

// Debug aids (the old abc_ui_cfefx probe driver): the module ABI carries no
// command arguments, so each action gets its own command and the accumulator
// is driven through the same entry point the netmsg sink uses. The damage
// amount for abc_ui_cfefx_damage comes from a cvar because there is no argv.
constexpr const char *kDebugAmountCvar = "abc_ui_cfefx_amount";
constexpr float kDebugAmountDefault = 8000.0f;

AbcUiCvarHandle DebugAmount = nullptr;

void debugDump(const char *label) {
  const float volume = cvarFloat(Volume, 0.25f);
  const float amount = cvarFloat(DebugAmount, kDebugAmountDefault);
  std::size_t flying = 0;
  for (const Pip &pip : Pips) {
    if (pip.starStart >= 0.0 &&
        ClientTimeNow < pip.starStart + kStarDuration)
      flying++;
  }
  Api->core->print(
      "[AbcUi] cfefx: %s enabled=%d active=%d damaged=%llu step=%llu "
      "tier=%d score=%d pending=%llu flying=%llu lastScore=%.2fs "
      "scoreAge=%.2fs amount=%.0f volume=%.2f\n",
      label, cvarFloat(Enabled, 1.0f) > 0.0f ? 1 : 0, hudActive() ? 1 : 0,
      static_cast<unsigned long long>(Damage),
      static_cast<unsigned long long>(damageStep()),
      static_cast<int>(LastTier), static_cast<int>(Score),
      static_cast<unsigned long long>(PendingStars),
      static_cast<unsigned long long>(flying),
      ClientTimeNow - LastScoreTime,
      ScoreStart < 0.0 ? -1.0 : ClientTimeNow - ScoreStart, amount, volume);
  if (!hudActive())
    Api->core->warn("[AbcUi] cfefx: inactive (no live player, health 0 or HUD "
                    "hidden) - the injection ran but nothing draws\n");
}

void debugStatus(void *) { debugDump("status"); }

void debugHit(void *) {
  onDamage(static_cast<int>(damageStep()));
  debugDump("hit");
}

void debugTier(void *) {
  // The old "tier 9" argument: clear the accumulator, then land exactly nine
  // marks so every side mark and its star queue are visible at once.
  reset();
  onDamage(static_cast<int>(damageStep() * 9));
  debugDump("tier");
}

void debugScore(void *) {
  // One full accumulator: ten stars queue up and the pop (NanoDamege1) shows
  // immediately, before the stars finish.
  onDamage(static_cast<int>(damageStep() * 10));
  debugDump("score");
}

void debugDamage(void *) {
  // The old "hit <value>" argument, driven by a cvar because commands carry no
  // argv. The whole star queue plays and the pop shows immediately once the
  // damage reaches a full hud_cfefx_max (8000 at the default max shows
  // NanoDamege8). The read value is echoed so a cvar that no longer holds the
  // intended number is visible in the console.
  if (!DebugAmount)
    DebugAmount = Api->console->findCvar(kDebugAmountCvar);
  const float requested = cvarFloat(DebugAmount, kDebugAmountDefault);
  const int amount =
      requested <= 0.0f ? 0 : static_cast<int>(std::min(requested, 1.0e6f));
  Api->core->print("[AbcUi] cfefx: injecting %d damage "
                   "(%s=%.0f, hud_cfefx_max=%.0f)\n",
                   amount, kDebugAmountCvar, requested,
                   cvarFloat(MaxDamage, 1000.0f));
  onDamage(amount);
  debugDump("damage");
}

void debugSound(void *) {
  playScoreSound();
  Api->core->print("[AbcUi] cfefx: sound requested at volume %.2f "
                   "(abc/cfefx/UI_SPECIALKILL2.wav)\n",
                   cvarFloat(Volume, 0.25f));
}

void debugReset(void *) {
  reset();
  debugDump("reset");
}

void registerDebugCommands() {
  DebugAmount = Api->console->registerCvar(kDebugAmountCvar, "8000",
                                           ABCUI_CVAR_VALUE);
  if (!DebugAmount)
    Api->core->warn("[AbcUi] cfefx: %s could not be registered; the debug "
                    "injector falls back to 8000\n",
                    kDebugAmountCvar);
  Api->console->registerCommand("abc_ui_cfefx_status", &debugStatus, nullptr);
  Api->console->registerCommand("abc_ui_cfefx_hit", &debugHit, nullptr);
  Api->console->registerCommand("abc_ui_cfefx_tier", &debugTier, nullptr);
  Api->console->registerCommand("abc_ui_cfefx_score", &debugScore, nullptr);
  Api->console->registerCommand("abc_ui_cfefx_damage", &debugDamage, nullptr);
  Api->console->registerCommand("abc_ui_cfefx_sound", &debugSound, nullptr);
  Api->console->registerCommand("abc_ui_cfefx_reset", &debugReset, nullptr);
}

#endif // NDEBUG

// ----------------------------------------------------------- lifecycle -----

void onModuleLoad() {
  PanelDesc.structSize = sizeof(PanelDesc);
  PanelDesc.name = kPanelName;
  PanelDesc.documentPath = kDocumentPath;
  PanelDesc.zIndex = 0;
  PanelDesc.userdata = nullptr;
  PanelDesc.onDocumentReady = &onDocumentReady;
  PanelDesc.onData = &onData;
  PanelDesc.onRedraw = nullptr;
  PanelDesc.reset = &onReset;
  Api->panels->registerPanel(&PanelDesc);

  // The old panel subscribed from onDocumentReady; the module subscribes once
  // per process and guards on the document handle, because the panel list is
  // rebuilt every HUD_Init while the bus lives for the whole process.
  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_POP_NUM, 0, &onPopNum, nullptr);
}

void onModuleInit() {
  // Old ctor defaults (persisted contract - never rename).
  Enabled = Api->console->registerCvar(kEnabledCvar, "1", ABCUI_CVAR_VALUE);
  MaxDamage = Api->console->registerCvarWatch(kMaxDamageCvar, "1000",
                                              ABCUI_CVAR_VALUE, &clampMaxDamage,
                                              nullptr);
  Volume = Api->console->registerCvarWatch(kVolumeCvar, "0.25",
                                           ABCUI_CVAR_VALUE, &clampVolume,
                                           nullptr);
  KillTime = Api->console->registerCvarWatch(kKillTimeCvar, "8",
                                             ABCUI_CVAR_VALUE, &clampKillTime,
                                             nullptr);
#ifndef NDEBUG
  registerDebugCommands();
#endif
}

void onModuleShutdown() {
  // The documents die with the context; every element handle must go.
  ScoreMark = nullptr;
  ScoreEffect = nullptr;
  for (Pip &pip : Pips) {
    pip.mark = nullptr;
    pip.star = nullptr;
    pip.starStart = -1.0;
  }
  HudActive = false;
  Damage = 0;
  LastTier = 0;
  Score = 0;
  LastScoreTime = 0.0;
  ScoreStart = -1.0;
  PendingStars = 0;
  NextStarStart = 0.0;
}

void onModuleUnload() { Api = nullptr; }

} // namespace

ABCUI_MODULE_EXPORT int AbcUiModule_Main(uint32_t abiVersion,
                                         const AbcUiModuleApi *api,
                                         AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api || !out)
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
  out->id = "abcui.cfefx";
  out->version = "1.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
