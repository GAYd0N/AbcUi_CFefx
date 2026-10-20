// modules::HudPanelCatalog implementation - see HudPanelCatalog.h.
#include "HudPanelCatalog.h"

#include "../framework/Commands.h"
#include "../framework/HudPanel.h"
#include "../framework/PanelManager.h"
#include "../data/NetworkMessages.h"
#include "../data/TextFeed.h"
#include "../panels/ammobar/AmmoBarPanel.h"
#include "../panels/ammostack/AmmoStackPanel.h"
#include "../panels/cfefx/CfefxPanel.h"
#include "../panels/chat/ChatPanel.h"
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
#include "../panels/motd/MotdPanel.h"
#include "../panels/notice/NoticePanel.h"
#include "../panels/playerboard/PlayerBoardPanel.h"
#include "../panels/popnum/PopNumPanel.h"
#include "../panels/radar/RadarPanel.h"
#include "../panels/scoreboard/ScoreboardPanel.h"
#include "../panels/sidetext/SideTextPanel.h"
#include "../panels/textmenu/TextMenuPanel.h"
#include "../panels/vote/VotePanel.h"
#include "../panels/weaponchoose/WeaponChoosePanel.h"
#include "../panels/weaponstack/WeaponStackPanel.h"

#include <memory>
#include <utility>
#include <vector>

namespace modules {
namespace {

std::vector<std::unique_ptr<framework::HudPanel>> buildPanels() {
  std::vector<std::unique_ptr<framework::HudPanel>> panels;
  panels.push_back(std::make_unique<panels::HealthPanel>());
  panels.push_back(std::make_unique<panels::FlashlightPanel>());
  panels.push_back(std::make_unique<panels::AmmoBarPanel>());
  panels.push_back(std::make_unique<panels::AmmoStackPanel>());
  panels.push_back(std::make_unique<panels::ItemStackPanel>());
  panels.push_back(std::make_unique<panels::WeaponStackPanel>());
  panels.push_back(std::make_unique<panels::DeadMsgPanel>());
  panels.push_back(std::make_unique<panels::NoticePanel>(
      "notice", "abc/abcui/hud/rml/notice.rml", textfeed::kPrintNotify));
  panels.push_back(std::make_unique<panels::NoticePanel>(
      "notice-center", "abc/abcui/hud/rml/notice_center.rml",
      textfeed::kPrintCenter));
  panels.push_back(std::make_unique<panels::SideTextPanel>());
  panels.push_back(std::make_unique<panels::TextMenuPanel>());
  // Batch B (feedback panels).
  panels.push_back(std::make_unique<panels::CrosshairPanel>());
  panels.push_back(std::make_unique<panels::IndicatorPanel>());
  panels.push_back(std::make_unique<panels::GIndicatorPanel>());
  panels.push_back(std::make_unique<panels::DmgTilesPanel>());
  panels.push_back(std::make_unique<panels::EffectPanel>());
  panels.push_back(std::make_unique<panels::PopNumPanel>());
  // Batch C (interaction + big data).
  panels.push_back(std::make_unique<panels::PlayerBoardPanel>());
  panels.push_back(std::make_unique<panels::VotePanel>());
  panels.push_back(std::make_unique<panels::MotdPanel>());
  panels.push_back(std::make_unique<panels::ScoreboardPanel>());
  panels.push_back(std::make_unique<panels::WeaponChoosePanel>());
  // Batch D (render depth).
  panels.push_back(std::make_unique<panels::ItemHighlightPanel>());
  panels.push_back(std::make_unique<panels::RadarPanel>());
  // Issue 13: chat after the readouts, so its log/input paints over them.
  panels.push_back(std::make_unique<panels::ChatPanel>());
  // Cfefx stays last so its damage/score feedback stacks above the HUD
  // panels (old CfefxPanel zpos 998/999).
  panels.push_back(std::make_unique<panels::CfefxPanel>());
  return panels;
}

} // namespace

void installHudPanels() {
  framework::PanelManager::installPanels(buildPanels());
}

void registerHudPanelCvars() {
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
  // Issue 13: after the client's HUD_Init ran (the hud_saytext_* pointers
  // are the client's own cvars).
  panels::chat::registerCvars();
  panels::cfefx::registerCvars();
}

} // namespace modules
