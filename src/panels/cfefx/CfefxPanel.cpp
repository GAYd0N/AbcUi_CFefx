// panels::CfefxPanel implementation - see CfefxPanel.h.
// from: ABCEnchance/src/hud/vgui/cfefx.* (GBK->UTF-8).
#include <metahook.h>

#include "CfefxPanel.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>

#include <algorithm>
#include <string>

#include <abc/CvarCmd.h>

#include "../../data/HudState.h"
#include "../../data/NetworkMessages.h"
#include "../../framework/Theme.h"

namespace panels {

namespace cfefx {

// Old ctor defaults (persisted contract - never rename).
cvar_t* Enabled = nullptr;    // hud_cfefx
cvar_t* MaxDamage = nullptr;  // hud_cfefx_max
cvar_t* Volume = nullptr;     // hud_cfefx_volume
cvar_t* KillTime = nullptr;   // hud_cfefx_time

void registerCvars() {
	Enabled = abc::CREATE_CVAR("hud_cfefx", "1", abc::FCVAR_VALUE, nullptr);
	MaxDamage = abc::CREATE_CVAR("hud_cfefx_max", "1000", abc::FCVAR_VALUE,
		[](cvar_t* cvar) {
			cvar->value = std::max<float>(cvar->value, 10.0f);
		});
	Volume = abc::CREATE_CVAR("hud_cfefx_volume", "0.25", abc::FCVAR_VALUE,
		[](cvar_t* cvar) {
			cvar->value = std::clamp(cvar->value, 0.0f, 1.0f);
		});
	KillTime = abc::CREATE_CVAR("hud_cfefx_time", "8", abc::FCVAR_VALUE,
		[](cvar_t* cvar) {
			cvar->value = std::max<float>(cvar->value, 0.0f);
		});
}

} // namespace cfefx

namespace {

// Document element ids, one per old vgui control.
constexpr const char* kMarkIds[9] = {
	"dmg-mark-one", "dmg-mark-two", "dmg-mark-three", "dmg-mark-four", "dmg-mark-five",
	"dmg-mark-six", "dmg-mark-seven", "dmg-mark-eight", "dmg-mark-nine",
};
constexpr const char* kStarIds[9] = {
	"star-one", "star-two", "star-three", "star-four", "star-five",
	"star-six", "star-seven", "star-eight", "star-nine",
};

// Old hudanimations.txt ypos c-98 / c-74 / c-50 / c-26 and DmgMarkFive c-122,
// expressed as absolute design-space top edges (480-line base).
constexpr float kPipTop[9] = {142.0f, 166.0f, 190.0f, 214.0f, 118.0f,
	142.0f, 166.0f, 190.0f, 214.0f};

// Document-relative, exactly like the rml's own src attributes: RmlUi joins a
// src against the document directory, so a mod-root path ("abc/abcui/...")
// resolves to abc/abcui/hud/rml/abc/... and the <img> falls back to the
// renderer's white placeholder quad while the geometry keeps animating.
constexpr const char* kScoreImages[10] = {
	"../img/cfefx/NanoDamege1.png",
	"../img/cfefx/NanoDamege2.png",
	"../img/cfefx/NanoDamege3.png",
	"../img/cfefx/NanoDamege4.png",
	"../img/cfefx/NanoDamege5.png",
	"../img/cfefx/NanoDamege6.png",
	"../img/cfefx/NanoDamege7.png",
	"../img/cfefx/NanoDamege8.png",
	"../img/cfefx/NanoDamege9.png",
	"../img/cfefx/NanoDamege10.png",
};

// Engine sound path (relative to the mod's sound/ directory).
constexpr const char* kScoreSound = "abc/cfefx/UI_SPECIALKILL2.wav";

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

float lerp(float from, float to, float t) {
	return from + (to - from) * t;
}

// Normalized 0..1 progress inside [start, start + duration].
float segment(double time, double start, double duration) {
	if (duration <= 0.0)
		return time >= start ? 1.0f : 0.0f;
	return std::clamp(static_cast<float>((time - start) / duration), 0.0f, 1.0f);
}

} // namespace

CfefxPanel::CfefxPanel()
	: HudPanel("cfefx", "abc/abcui/hud/rml/cfefx.rml") {}

void CfefxPanel::onDocumentReady() {
	ScoreMark = element("score-mark");
	ScoreEffect = element("score-effect");
	for (std::size_t i = 0; i < 9; i++) {
		Pips[i].mark = element(kMarkIds[i]);
		Pips[i].star = element(kStarIds[i]);
	}

	reset();

	// The old Viewport called AddDmg for every POPNUMBER package independently
	// of the hud_popnumber front-facing gate, so the panel subscribes to the
	// raw data event instead of borrowing PopNumPanel's filtered view.
	subscribe(netmsg::PopNum, [this](const netmsg::PopNumData& data) {
		onDamage(data.value);
	});
}

void CfefxPanel::reset() {
	HudActive = false;
	Damage = 0;
	LastTier = 0;
	Score = 0;
	LastScoreTime = 0.0;
	ScoreStart = -1.0;

	resetAllMarks();
	resetScoreEffect();
	hide();
}

#ifndef NDEBUG
CfefxPanel::DebugState CfefxPanel::debugState() const {
	DebugState state;
	state.clock = ClientTimeNow;
	state.damage = Damage;
	state.tier = LastTier;
	state.score = Score;
	state.lastScoreAge = ClientTimeNow - LastScoreTime;
	state.scoreAge = ScoreStart < 0.0 ? -1.0 : ClientTimeNow - ScoreStart;
	state.enabled = cfefx::Enabled && cfefx::Enabled->value > 0.0f;
	// Same expression evaluate() uses for the tier step, so the driver can
	// inject exactly one mark's worth of damage.
	if (cfefx::MaxDamage)
		state.step = static_cast<int>(std::max<float>(10.0f, cfefx::MaxDamage->value)) / 10;
	state.volume = cfefx::Volume ? cfefx::Volume->value : 0.0f;
	state.active = hudActive();
	state.visible = isVisible();
	return state;
}
#endif

void CfefxPanel::onDamage(int value) {
	if (!cfefx::Enabled || cfefx::Enabled->value <= 0.0f)
		return;
	// Old AddDmg only accumulated positive numbers; heals still ran the
	// visibility/tier evaluation.
	if (value > 0)
		Damage += static_cast<std::uint64_t>(value);
	evaluate();
}

void CfefxPanel::evaluate() {
	if (!hudActive()) {
		if (HudActive)
			reset();
		return;
	}
	HudActive = true;

	show();

	if (!cfefx::MaxDamage)
		return;
	const std::uint64_t step =
		static_cast<std::uint64_t>(std::max<float>(10.0f, cfefx::MaxDamage->value)) / 10;
	if (step == 0 || Damage < step)
		return;

	const std::size_t tier = static_cast<std::size_t>(Damage / step);
	if (tier == 0 || tier == LastTier)
		return;
	const std::size_t previous = LastTier;
	LastTier = tier;

	if (tier >= 10) {
		// Tenth tenth: score pop, clear the marks, reset the accumulator.
		LastTier = 0;
		Damage = 0;
		resetAllMarks();

		// Old combo window: reset only when the next score arrives after the
		// timeout - the score image itself is transient either way.
		if (cfefx::KillTime && ClientTimeNow - LastScoreTime > cfefx::KillTime->value &&
			Score != 0)
			Score = 0;

		playScoreSound();
		updateScoreEffect();
		LastScoreTime = ClientTimeNow;
		if (Score < 9)
			Score++;
		return;
	}

	// Queue every tenth unlocked by this hit, in order - a single 400-damage
	// hit therefore plays four stars 0.05 s apart instead of one synchronized
	// burst. Re-walking already-lit pips would restart their animations.
	if (tier < 5) {
		// First tier: marks 1-4. Clear the second-row marks.
		for (std::size_t i = 4; i < 9; i++)
			resetDmgMark(i);
	}
	for (std::size_t i = previous; i < tier; i++)
		updateDmgMark(i);
	if (tier >= 5) {
		// The 500 mark replaces marks 1-4. Their stars keep flying (the queue
		// stays continuous), but their side marks are interrupted here.
		for (std::size_t i = 0; i < 4; i++)
			resetSideMark(i, true);
	}
}

void CfefxPanel::updateDmgMark(std::size_t index) {
	if (index >= 9)
		return;
	Pip& pip = Pips[index];
	if (!pip.mark || !pip.star)
		return;

	// A new action re-arms its own side mark even if it is already visible
	// (the old side is overwritten immediately) and joins the tail of the
	// star queue.
	resetSideMark(index, false);
	const double start = std::max(ClientTimeNow, NextStarStart);
	pip.starStart = start;
	NextStarStart = start + kStarStagger;
}

void CfefxPanel::resetSideMark(std::size_t index, bool suppress) {
	if (index >= 9)
		return;
	Pip& pip = Pips[index];
	pip.sideSuppressed = suppress;
	setOpacity(pip.mark, 0.0f);
}

void CfefxPanel::resetDmgMark(std::size_t index) {
	if (index >= 9)
		return;
	Pip& pip = Pips[index];
	pip.starStart = -1.0;
	pip.sideSuppressed = false;
	setOpacity(pip.mark, 0.0f);
	setOpacity(pip.star, 0.0f);

	float width = 0.0f, height = 0.0f, scale = 1.0f;
	if (pip.star && screenMetrics(width, height, scale)) {
		const float half = kStarSize * 0.5f * scale;
		setBox(pip.star, width * 0.5f - half, height * 0.5f - half,
			kStarSize * scale, kStarSize * scale);
	}
	rebaseStarQueue();
}

// Keep the queue cursor behind the last scheduled star after a reset removed
// one of its entries, so the remaining actions keep their 0.05 s cadence.
void CfefxPanel::rebaseStarQueue() {
	double latest = -1.0;
	for (const Pip& pip : Pips) {
		if (pip.starStart >= 0.0)
			latest = std::max(latest, pip.starStart);
	}
	NextStarStart = latest < 0.0 ? ClientTimeNow
		: std::max(ClientTimeNow, latest + kStarStagger);
}

void CfefxPanel::resetAllMarks() {
	for (std::size_t i = 0; i < 9; i++)
		resetDmgMark(i);
}

void CfefxPanel::updateScoreEffect() {
	ScoreStart = ClientTimeNow;
	if (ScoreMark)
		ScoreMark->SetAttribute("src", kScoreImages[std::min<std::size_t>(Score, 9)]);
}

void CfefxPanel::resetScoreEffect() {
	ScoreStart = -1.0;
	setOpacity(ScoreMark, 0.0f);
	setOpacity(ScoreEffect, 0.0f);
	if (ScoreMark)
		ScoreMark->SetAttribute("src", kScoreImages[0]);
}

bool CfefxPanel::hudActive() const {
	if (!Document || !cfefx::Enabled || cfefx::Enabled->value <= 0.0f)
		return false;
	if (!gEngfuncs.GetLocalPlayer())
		return false;
	if (hudstate::isHudHidden(hudstate::kHideAll | hudstate::kHideWeapons))
		return false;
	return hudstate::health() > 0.0f;
}

bool CfefxPanel::screenMetrics(float& width, float& height, float& scale) const {
	if (!Document)
		return false;
	Rml::Context* context = Document->GetContext();
	if (!context)
		return false;
	const Rml::Vector2i dimensions = context->GetDimensions();
	if (dimensions.x <= 0 || dimensions.y <= 0)
		return false;
	width = static_cast<float>(dimensions.x);
	height = static_cast<float>(dimensions.y);
	scale = framework::Theme::scale();
	return true;
}

void CfefxPanel::onData(double frameTime) {
	ClientTimeNow += frameTime;

	if (!hudActive()) {
		if (HudActive)
			reset();
		return;
	}
	HudActive = true;
	if (!isVisible())
		show();

	animateScore(ClientTimeNow);
	for (std::size_t i = 0; i < 9; i++)
		animatePip(Pips[i], i, ClientTimeNow);
}

void CfefxPanel::animatePip(Pip& pip, std::size_t index, double now) {
	if (!pip.mark || !pip.star)
		return;

	float width = 0.0f, height = 0.0f, scale = 1.0f;
	if (!screenMetrics(width, height, scale))
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
		// fully opaque - the original has no fade on this transition. A side
		// replaced by the 500 mark stays suppressed while its star finishes.
		setOpacity(pip.star, 0.0f);
		setOpacity(pip.mark, pip.sideSuppressed ? 0.0f : 1.0f);
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
	}
	else if (elapsed < 0.175) {
		x = midX;
		y = top;
		size = bigSize;
	}
	else {
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

void CfefxPanel::animateScore(double now) {
	if (ScoreStart < 0.0)
		return;

	float width = 0.0f, height = 0.0f, scale = 1.0f;
	if (!screenMetrics(width, height, scale))
		return;

	const double elapsed = now - ScoreStart;
	const float baseX = width * 0.5f + kScoreCenterOffset * scale;
	const float baseY = kScoreTop * scale;
	const float baseW = kScoreWidth * scale;
	const float baseH = kScoreHeight * scale;
	(void)height; // score geometry is height-relative through Theme::scale()

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
		}
		else if (elapsed < 0.09) {
			const float t = segment(elapsed, 0.06, 0.03);
			x = lerp(baseX - 36.0f * scale, baseX, t);
			y = lerp(baseY - 30.0f * scale, baseY, t);
			w = lerp(168.0f * scale, baseW, t);
			h = lerp(140.0f * scale, baseH, t);
		}
		else if (elapsed < 0.11) {
			const float t = segment(elapsed, 0.09, 0.02);
			x = lerp(baseX, baseX + 3.0f * scale, t);
			y = lerp(baseY, baseY - 3.0f * scale, t);
		}
		else if (elapsed < 0.13) {
			const float t = segment(elapsed, 0.11, 0.02);
			x = lerp(baseX + 3.0f * scale, baseX - 3.0f * scale, t);
			y = lerp(baseY - 3.0f * scale, baseY + 3.0f * scale, t);
		}
		else if (elapsed < 0.15) {
			const float t = segment(elapsed, 0.13, 0.02);
			x = lerp(baseX - 3.0f * scale, baseX + 2.0f * scale, t);
			y = lerp(baseY + 3.0f * scale, baseY + 2.0f * scale, t);
		}
		else if (elapsed < 0.17) {
			const float t = segment(elapsed, 0.15, 0.02);
			x = lerp(baseX + 2.0f * scale, baseX, t);
			y = lerp(baseY + 2.0f * scale, baseY, t);
		}

		const float alpha = 1.0f - segment(elapsed, 1.5, 0.5);
		setBox(ScoreMark, x, y, w, h);
		setOpacity(ScoreMark, alpha);
	}
	else {
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
		}
		else if (elapsed < 0.7) {
			alpha = 1.0f;
			const float t = segment(elapsed, 0.2, 0.5);
			x = lerp(startX, targetX, t);
			y = lerp(startY, targetY, t);
		}
		else if (elapsed < 0.9) {
			alpha = 1.0f;
			x = targetX;
			y = targetY;
			const float t = segment(elapsed, 0.7, 0.2);
			w = lerp(kScoreEffectWidth * scale, targetW, t);
			h = lerp(kScoreEffectHeight * scale, targetH, t);
		}
		else {
			alpha = 1.0f - segment(elapsed, 0.9, 0.3);
			x = targetX;
			y = targetY;
			w = targetW;
			h = targetH;
		}

		setBox(ScoreEffect, x, y, w, h);
		setOpacity(ScoreEffect, alpha * kScoreEffectBaseAlpha);
	}
	else {
		setOpacity(ScoreEffect, 0.0f);
	}

	// The score mark is the longest-lived piece (fade ends at 2.0s); only then
	// is the whole score animation retired.
	if (elapsed >= kScoreMarkEnd)
		ScoreStart = -1.0;
}

void CfefxPanel::playScoreSound() const {
	if (!cfefx::Volume || cfefx::Volume->value <= 0.0f)
		return;
	if (gEngfuncs.pfnPlaySoundByName)
		gEngfuncs.pfnPlaySoundByName(kScoreSound, cfefx::Volume->value);
}

void CfefxPanel::setBox(Rml::Element* element, float left, float top, float width,
	float height) {
	if (!element)
		return;
	element->SetProperty("left", std::to_string(left) + "px");
	element->SetProperty("top", std::to_string(top) + "px");
	element->SetProperty("width", std::to_string(width) + "px");
	element->SetProperty("height", std::to_string(height) + "px");
}

void CfefxPanel::setOpacity(Rml::Element* element, float opacity) {
	if (!element)
		return;
	element->SetProperty("opacity", std::to_string(std::clamp(opacity, 0.0f, 1.0f)));
}

}
