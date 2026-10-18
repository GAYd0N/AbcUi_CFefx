// panels::Vote implementation - see VotePanel.h.
// from: ABCEnchance/src/hud/vgui/vote.cpp (GBK->UTF-8).
// D-17 note: the old StartVote rebuilt typed votes (kill/kick/ban/map)
// through English-template regex extraction + localize reassembly; a
// non-English server text silently fell back to the raw string anyway. The
// port shows the server text directly (the server owns its localization)
// and keeps the localized default captions next to the bound key names.
#include <metahook.h>

#include "VotePanel.h"

#include <RmlUi/Core/Element.h>

#include <abc/CvarCmd.h>
#include <abc/Format.h>

#include "../../data/NetworkMessages.h"
#include "../../framework/PanelManager.h"
#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"
#include "../../rmlui/VGUI2ExtensionImport.h"

namespace panels {

namespace vote {

// Old CVotePanel ctor defaults (persisted contract - never rename).
cvar_t *HudVote = nullptr;
cvar_t *KeyYes = nullptr;
cvar_t *KeyNo = nullptr;
namespace {
VotePanel *instance() {
  return static_cast<VotePanel *>(framework::PanelManager::find("vote"));
}
} // namespace

void registerCvars() {
  HudVote = abc::CREATE_CVAR("hud_vote", "1", abc::FCVAR_VALUE, nullptr);
  KeyYes = abc::CREATE_CVAR("hud_votekey_yes", "92", abc::FCVAR_VALUE, nullptr);
  KeyNo = abc::CREATE_CVAR("hud_votekey_no", "93", abc::FCVAR_VALUE, nullptr);
  // The VoteMenu usermsg consumer passes messages through while the gate
  // is off (old IsVoteEnable).
  netmsg::setVoteGate(HudVote);
}

bool onKeyEvent(int down, int keynum) {
  // Old Viewport::KeyInput -> CVotePanel::KeyCodeTyped.
  VotePanel *panel = instance();
  return panel && panel->answerKey(down, keynum);
}

} // namespace vote

namespace {

std::string defaultCaption(const char *token, const char *fallback) {
  std::string out;
  return framework::Localization::translate(out, token) ? out : fallback;
}

} // namespace

VotePanel::VotePanel() : HudPanel("vote", "abc/abcui/hud/rml/vote.rml") {}

VotePanel::~VotePanel() = default;

void VotePanel::reset() {
  setVisible(false);
}

void VotePanel::setVisible(bool visible) {
  if (visible == Visible)
    return;
  Visible = visible;
  visible ? show() : hide();
}

void VotePanel::onData(double) {
  if (vote::HudVote && vote::HudVote->value <= 0)
    setVisible(false);
}

bool VotePanel::answerKey(int down, int keynum) {
  if (!down || !Visible || !isVisible() || !vote::HudVote ||
      vote::HudVote->value <= 0)
    return false;
  const int yes = vote::KeyYes ? static_cast<int>(vote::KeyYes->value) : 92;
  const int no = vote::KeyNo ? static_cast<int>(vote::KeyNo->value) : 93;
  if (keynum != yes && keynum != no)
    return false;
  // Old KeyCodeTyped sent ServerCmd("voteyes"/"voteno").
  gEngfuncs.pfnServerCmd(keynum == yes ? "voteyes" : "voteno");
  setVisible(false);
  return true;
}

void VotePanel::startVote(int type, const std::string &content,
                          const std::string &yes, const std::string &no) {
  (void)type;
  if (Rml::Element *node = element("content"))
    node->SetInnerRML(framework::TextUtil::escapeRml(content));

  // Server captions (CUSTOM votes) win; typed/default votes keep the
  // localized default caption next to the bound key name.
  const std::string yesCaption =
      !yes.empty() ? yes : defaultCaption("Vote_DefaultYes", "Yes");
  const std::string noCaption =
      !no.empty() ? no : defaultCaption("Vote_DefaultNo", "No");
  // The caption is server text - bounded assembly only (R-17 face).
  const auto boundKey = [](int keynum) {
    const char *name = vgui2ext::keyNameForKey(keynum);
    return name ? name : "";
  };
  const std::string yesLabel = abc::Format(
      "{0} {1}",
      boundKey(vote::KeyYes ? static_cast<int>(vote::KeyYes->value) : 92),
      yesCaption);
  const std::string noLabel = abc::Format(
      "{0} {1}",
      boundKey(vote::KeyNo ? static_cast<int>(vote::KeyNo->value) : 93),
      noCaption);
  if (Rml::Element *text = element("yes-text"))
    text->SetInnerRML(framework::TextUtil::escapeRml(yesLabel));
  if (Rml::Element *text = element("no-text"))
    text->SetInnerRML(framework::TextUtil::escapeRml(noLabel));

  setVisible(true);
}

void VotePanel::onDocumentReady() {
  // Old ctor label: the static "#Vote_DefaultMessage" title strip.
  if (Rml::Element *node = element("title"))
    node->SetInnerRML(framework::TextUtil::escapeRml(
        defaultCaption("Vote_DefaultMessage", "")));
  subscribe(netmsg::VoteMenu, [this](const netmsg::VoteMenuData &data) {
    startVote(data.type, data.content, data.yes, data.no);
  });
  subscribe(netmsg::EndVote, [this]() { setVisible(false); });
  setVisible(false);
}

} // namespace panels
