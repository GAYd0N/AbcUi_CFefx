#pragma once

// panels::Cfefx - cumulative damage / kill-streak HUD feedback.
// from: ABCEnchance/src/hud/vgui/cfefx.* (GBK->UTF-8).
//
// Old control mapping: one VGUI CCfefxPanel with nine damage-mark ImagePanels,
// nine star ImagePanels, a score mark and a score effect becomes one RmlUi
// document. The VGUI AnimationController sequences in hudanimations.txt are
// reproduced with timestamps in onData (the DmgTiles instrument), and the old
// 854x480 absolute animation coordinates are re-anchored to the live viewport
// so the effect follows Theme::scale on any aspect ratio.
//
// State: every 1/10 of hud_cfefx_max damage lights one of nine marks; the
// tenth tenth triggers a score pop (NanoDamege1..10), a sound and resets the
// marks. A score within hud_cfefx_time of the previous one advances the combo;
// otherwise it starts again at 1. The old plugin called this UpdateAnimations
// from its AddDmg path - the panel now owns the whole loop and its reset.

#include "../../framework/HudPanel.h"

#include <cstddef>
#include <cstdint>

namespace panels {

namespace cfefx {
// HUD_Init-time cvar registration (plan 08 §4.9).
void registerCvars();
}

class CfefxPanel : public framework::HudPanel {
public:
	CfefxPanel();

	void reset() override;

protected:
	void onDocumentReady() override;
	void onData(double frameTime) override;

private:
	struct Pip {
		Rml::Element* mark = nullptr;
		Rml::Element* star = nullptr;
		double markStart = -1.0;
		double starStart = -1.0;
	};

	void onDamage(int value);
	void evaluate();

	void updateDmgMark(std::size_t index);
	void resetDmgMark(std::size_t index);
	void resetAllMarks();
	void updateScoreEffect();
	void resetScoreEffect();

	bool hudActive() const;
	bool screenMetrics(float& width, float& height, float& scale) const;
	void animatePip(Pip& pip, std::size_t index, double now);
	void animateScore(double now);
	void playScoreSound() const;

	static void setBox(Rml::Element* element, float left, float top, float width, float height);
	static void setOpacity(Rml::Element* element, float opacity);

	double ClientTimeNow = 0.0;
	std::uint64_t Damage = 0;
	std::size_t LastTier = 0;
	std::size_t Score = 0;
	double LastScoreTime = 0.0;
	double ScoreStart = -1.0;

	Pip Pips[9];
	Rml::Element* ScoreMark = nullptr;
	Rml::Element* ScoreEffect = nullptr;
	// Tracks the last hudActive() result so reset() runs on the transition into
	// the inactive state instead of rebuilding an already-clean panel per frame.
	bool HudActive = false;
};

}
