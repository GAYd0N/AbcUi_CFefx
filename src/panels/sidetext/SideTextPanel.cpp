// panels::SideTextPanel implementation - see SideTextPanel.h.
// from: ABCEnchance/src/hud/vgui/sidetext.* (GBK->UTF-8).
#include <metahook.h>

#include "SideTextPanel.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>

#include <abc/CvarCmd.h>

// Windows.h min/max macros fight std::max (old repo precedent).
#undef max
#undef min

#include "../../data/HudState.h"
#include "../../data/NetworkMessages.h"
#include "../../framework/GlyphText.h"

namespace panels {

namespace sidetext {

// Old CSidePanel ctor default (persisted contract - never rename).
cvar_t *SideInfo = nullptr;

void registerCvars() {
  SideInfo = abc::CREATE_CVAR("hud_sideinfo", "1", abc::FCVAR_VALUE, nullptr);
}

} // namespace sidetext

namespace {
// Old SideTextPanel.res Message box: 60dp wide centered in the 72dp image.
constexpr int kTextBoxWidth = 60;

void replaceAll(std::string &subject, const char *key,
                const std::string &value) {
  const size_t keyLength = strlen(key);
  size_t pos = 0;
  while ((pos = subject.find(key, pos)) != std::string::npos) {
    subject.replace(pos, keyLength, value);
    pos += value.length();
  }
}
} // namespace

SideTextPanel::SideTextPanel()
    : HudPanel("sidetext", "abc/abcui/hud/rml/sidetext.rml") {}

void SideTextPanel::onDocumentReady() {
  // The template is the document's initial text (old template_text res
  // entry; the shipped default is "${SCORE}" which carries {SCORE}).
  if (Rml::Element *message = element("message")) {
    Template = message->GetInnerRML();
    TemplateLoaded = !Template.empty();
  }

  subscribe(netmsg::ScoreInfo, [this](int clientIndex, float frags, int, float,
                                      float, int, int, int) {
    const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
    if (local && clientIndex == local->index)
      LocalFrags = static_cast<int>(frags);
  });
  subscribe(hudstate::SuitChanged, [this](bool) { updateVisibility(); });
}

void SideTextPanel::onData(double frameTime) {
  (void)frameTime;
  // Old OnThink evaluated the cvar live (hud_sideinfo callback parity).
  updateVisibility();
  if (!isVisible())
    return;

  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (!local || !TemplateLoaded)
    return;

  const float *velocity = local->curstate.velocity;
  // Old: Vector(velocity).AsVector2D().Length() - horizontal speed only
  // (vertical velocity is not part of the old readout).
  const float speed =
      sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
  MaxSpeed = std::max(MaxSpeed, speed);

  std::string text = Template;
  replaceAll(text, "{SCORE}", std::to_string(LocalFrags));
  replaceAll(text, "{MAXSPEED}", std::to_string(static_cast<int>(MaxSpeed)));
  replaceAll(text, "{SPEED}", std::to_string(static_cast<int>(speed)));
  if (text != LastText) {
    LastText = text;
    if (Rml::Element *message = element("message")) {
      // Ink-only additive glyphs (no black outline layer - the score sits on
      // the scene like the flashlight percentage); a template with characters
      // outside the glyph set falls back to plain text.
      const std::string ink =
          framework::glyphs::inkMarkup(text, framework::glyphs::kAccentTint);
      message->SetInnerRML(ink.empty() ? text : ink);
    }
  }
}

void SideTextPanel::reset() {
  // Old Reset: hide + max-speed accumulator back to zero.
  hide();
  MaxSpeed = 0.0f;
  LastText.clear();
}

void SideTextPanel::updateVisibility() {
  // Old hud_sideinfo cvar + suit gate; the old panel also hid with the
  // Viewport-wide HIDEALL token (see HealthPanel note).
  const bool visible = hudstate::hasSuit() &&
                       (!sidetext::SideInfo || sidetext::SideInfo->value > 0) &&
                       !hudstate::isHudHidden(hudstate::kHideAll);
  if (visible)
    show();
  else
    hide();
}

} // namespace panels
