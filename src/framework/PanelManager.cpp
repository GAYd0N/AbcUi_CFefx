// framework::PanelManager implementation - see PanelManager.h.
#include "PanelManager.h"

#include "Commands.h"
#include "HudPanel.h"
#include "../data/HudState.h"

#include "../data/NetworkMessages.h"
#include "../data/TextFeed.h"
#include "../panels/ammostack/AmmoStackPanel.h"
#include "../panels/ammobar/AmmoBarPanel.h"
#include "../panels/crosshair/CrosshairPanel.h"
#include "../panels/deadmsg/DeadMsgPanel.h"
#include "../panels/dmgtiles/DmgTilesPanel.h"
#include "../panels/effect/EffectPanel.h"
#include "../panels/flashlight/FlashlightPanel.h"
#include "../panels/gindicator/GIndicatorPanel.h"
#include "../panels/health/HealthPanel.h"
#include "../panels/indicator/IndicatorPanel.h"
#include "../panels/itemhighlight/ItemHighlightPanel.h"
#include "../panels/itemstack/ItemStackPanel.h"
#include "../panels/notice/NoticePanel.h"
#include "../panels/motd/MotdPanel.h"
#include "../panels/scoreboard/ScoreboardPanel.h"
#include "../panels/playerboard/PlayerBoardPanel.h"
#include "../panels/vote/VotePanel.h"
#include "../panels/weaponchoose/WeaponChoosePanel.h"
#include "../panels/popnum/PopNumPanel.h"
#include "../panels/cfefx/CfefxPanel.h"
#include "../panels/radar/RadarPanel.h"
#include "../panels/sidetext/SideTextPanel.h"
#include "../panels/textmenu/TextMenuPanel.h"
#include "../panels/weaponstack/WeaponStackPanel.h"

#include <abc/Console.h>

namespace framework {

static std::vector<std::unique_ptr<HudPanel>> createPanels() {
	// Factory list: one line per panel (plan 05 §4.1 - Viewport successor).
	return []() {
		std::vector<std::unique_ptr<HudPanel>> list;
		list.push_back(std::make_unique<panels::HealthPanel>());
		list.push_back(std::make_unique<panels::FlashlightPanel>());
		list.push_back(std::make_unique<panels::AmmoBarPanel>());
		list.push_back(std::make_unique<panels::AmmoStackPanel>());
		list.push_back(std::make_unique<panels::ItemStackPanel>());
		list.push_back(std::make_unique<panels::WeaponStackPanel>());
		list.push_back(std::make_unique<panels::DeadMsgPanel>());
		list.push_back(std::make_unique<panels::NoticePanel>("notice", "abc/abcui/hud/rml/notice.rml", textfeed::kPrintNotify));
		list.push_back(std::make_unique<panels::NoticePanel>("notice-center", "abc/abcui/hud/rml/notice_center.rml", textfeed::kPrintCenter));
		list.push_back(std::make_unique<panels::SideTextPanel>());
		list.push_back(std::make_unique<panels::TextMenuPanel>());
		// Batch B (feedback panels, plan 05 §6).
		list.push_back(std::make_unique<panels::CrosshairPanel>());
		list.push_back(std::make_unique<panels::IndicatorPanel>());
		list.push_back(std::make_unique<panels::GIndicatorPanel>());
		list.push_back(std::make_unique<panels::DmgTilesPanel>());
		list.push_back(std::make_unique<panels::EffectPanel>());
		list.push_back(std::make_unique<panels::PopNumPanel>());
		// Batch C (interaction + big data, plan 05 §6).
		list.push_back(std::make_unique<panels::PlayerBoardPanel>());
		list.push_back(std::make_unique<panels::VotePanel>());
		list.push_back(std::make_unique<panels::MotdPanel>());
		list.push_back(std::make_unique<panels::ScoreboardPanel>());
		list.push_back(std::make_unique<panels::WeaponChoosePanel>());
		// Batch D (render depth, plan 05 §6).
		list.push_back(std::make_unique<panels::ItemHighlightPanel>());
		list.push_back(std::make_unique<panels::RadarPanel>());
		// Cfefx is last so its damage/score feedback stacks above the HUD
		// panels (old CfefxPanel zpos 998/999).
		list.push_back(std::make_unique<panels::CfefxPanel>());
		return list;
	}();
}

std::vector<std::unique_ptr<HudPanel>>& PanelManager::Panels() {
	static std::vector<std::unique_ptr<HudPanel>> panels;
	return panels;
}

void PanelManager::registerPanelCvars() {
	// HUD_Init-time cvar registration (plan 08 §4.9); panel namespaces own
	// their cvar pointers, the spine only triggers the batch.
	panels::health::registerCvars();
	panels::deadmsg::registerCvars();
	panels::sidetext::registerCvars();
	commands::registerAll();
	// Batch B.
	panels::crosshair::registerCvars();
	panels::indicator::registerCvars();
	panels::gindicator::registerCvars();
	panels::effect::registerCvars();
	panels::popnum::registerCvars();
	panels::cfefx::registerCvars();
	// Batch C.
	panels::playerboard::registerCvars();
	panels::vote::registerCvars();
	panels::motd::registerCvars();
	panels::scoreboard::registerCvars();
	panels::weaponchoose::registerCvars();
	panels::weaponchoose::registerCommands();
	// Batch D.
	panels::itemhighlight::registerCvars();
	panels::radar::registerCvars();
	panels::radar::registerCommands();
}

void PanelManager::initAll(Rml::Context* context) {
	if (!context)
		return;
	if (Panels().empty()) Panels() = createPanels();
	for (auto& panel : Panels()) {
		panel->loadDocument(context);
		panel->setSessionActive(SessionActive);
	}
	abc::ConsolePrint("[AbcUi] %d panel(s) loaded\n", static_cast<int>(Panels().size()));
}

void PanelManager::vidInitAll() {
	// Old Viewport::VidInit ended with Reset(): every panel back to its
	// pre-map state on every map load / video init.
	for (auto& panel : Panels())
		panel->reset();
}

void PanelManager::shutdown() {
	Panels().clear();
	SessionActive = false;
}

}
