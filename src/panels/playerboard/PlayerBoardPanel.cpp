// panels::PlayerBoard implementation - see PlayerBoardPanel.h.
// from: ABCEnchance/src/hud/vgui/playerboard.cpp (GBK->UTF-8). The old
// vgui one-panel-per-player fleet (SetId per index, every tile Thinks every
// frame) becomes one document re-targeted per frame - the observable result
// (all visible teammates carry a tile) is the same and the DOM stays tiny.
#include <metahook.h>

#include "PlayerBoardPanel.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementUtilities.h>

#include <algorithm>
#include <cmath>

#include <abc/CvarCmd.h>

#include "../../data/HudState.h"
#include "../../data/PlayerResource.h"
#include "../../data/WeaponResource.h"
#include "../../framework/ColorUtil.h"
#include "../../framework/Projection.h"
#include "../../framework/TextUtil.h"
#include "../../hooks.h"

#include <abc/Math.h>

// Windows.h min/max macros fight std::min (old repo precedent).
#undef max
#undef min

namespace panels {

namespace playerboard {

// Old Viewport ctor cvars (persisted contract - never rename).
cvar_t *PlayerInfo = nullptr;
cvar_t *PlayerInfoDanger = nullptr;

void registerCvars() {
  PlayerInfo =
      abc::CREATE_CVAR("hud_playerinfo", "1", abc::FCVAR_VALUE, nullptr);
  PlayerInfoDanger = abc::CREATE_CVAR("hud_playerinfo_danger", "30",
                                      abc::FCVAR_VALUE, nullptr);
}

} // namespace playerboard

namespace {

// Old Think gates (cos 60 cone, 1024u range).
constexpr float kMaxDistance = 1024.0f;
constexpr float kMinViewDot = 0.5f;
// Old SetPos centered the tile on the projection (x - GetWide()/2, wide 80).
constexpr float kTileHalfWidth = 40.0f;

// Per-tile inner-element lookup. The 32 tiles intentionally repeat the same
// inner ids ("name", "bar-health", ...) in one document, so the lookup MUST
// stay subtree-scoped: Element::GetElementById resolves from the owner
// document (RmlUi Element.cpp GetElementById -> GetOwnerDocument), which
// would collapse every tile's update into tile-1's elements - all tiles
// showing one player (the P6 user-test report).
Rml::Element *child(Rml::Element *root, const char *id) {
  return root ? Rml::ElementUtilities::GetElementById(root, id) : nullptr;
}

// Document element ids "tile-1".."tile-32" (rml generation matches).
std::string TileIds[playerres::kMaxPlayers + 1];
bool TileIdsReady = false;

const char *tileId(int index) {
  if (!TileIdsReady) {
    for (int i = 1; i <= playerres::kMaxPlayers; i++)
      TileIds[i] = "tile-" + std::to_string(i);
    TileIdsReady = true;
  }
  return TileIds[index].c_str();
}

void setBarWidth(Rml::Element *bar, float ratio) {
  if (!bar)
    return;
  // Old fill = BackGroundImage wide (67 in PlayerBoardPanel.res; the
  // HealthImage 38 was only the initial value, overwritten per Think).
  const float width = 67.0f * std::clamp(ratio, 0.0f, 1.0f);
  bar->SetProperty("width", std::to_string(width) + "dp");
}

} // namespace

PlayerBoardPanel::PlayerBoardPanel()
    : HudPanel("playerboard", "abc/abcui/hud/rml/playerboard.rml") {}

void PlayerBoardPanel::onDocumentReady() {}

void PlayerBoardPanel::reset() {
  AppliedDom.clear();
  if (!Document)
    return;
  for (int index = 1; index <= playerres::kMaxPlayers; index++) {
    if (Rml::Element *tile = child(Document, tileId(index)))
      AppliedDom.property(tile, "display", "none");
  }
}

bool PlayerBoardPanel::updateClientInfo(Rml::Element *tile, int entindex) {
  const playerres::PlayerInfo *info = playerres::getPlayerInfo(entindex);
  const playerres::PlayerInfo *local = playerres::getLocalPlayerInfo();
  if (!info || !local || !info->isValid()) {
    return false;
  }
  if (info->spectate || info->team != local->team) {
    return false;
  }

  const float health = info->health;
  const float armorRatio = std::clamp(info->armor / 100.0f, 0.0f, 1.0f);
  const float healthRatio = std::clamp(health / 100.0f, 0.0f, 1.0f);

  Rml::Element *name = child(tile, "name");
  if (name) {
    AppliedDom.text(name, framework::TextUtil::escapeRml(info->name));
    // Old SetFgColor(GetPlayerColor(index)): the client dll's per-player
    // tint (0..1 floats -> rgba byte triple, alpha 255).
    if (hooks::GetClientColor) {
      const float *tint = hooks::GetClientColor(entindex);
      if (tint) {
        const std::string color = framework::ColorUtil::formatRgba(
            static_cast<int>(std::clamp(tint[0], 0.0f, 1.0f) * 255.0f),
            static_cast<int>(std::clamp(tint[1], 0.0f, 1.0f) * 255.0f),
            static_cast<int>(std::clamp(tint[2], 0.0f, 1.0f) * 255.0f), 255);
        AppliedDom.property(name, "color", color);
      }
    }
  }
  AppliedDom.property(child(tile, "bar-health"), "width",
                      std::to_string(67.0f * healthRatio) + "dp");
  AppliedDom.property(child(tile, "bar-armor"), "width",
                      std::to_string(67.0f * armorRatio) + "dp");

  // Danger band icons: below hud_playerinfo_danger - dead -> skull, hurt
  // -> medkit; otherwise the crouch icon rides the hull height (old
  // maxs.z - mins.z < 64; the danger cvar read rides its old default when
  // the pointer is missing).
  const cl_entity_t *ent = gEngfuncs.GetEntityByIndex(entindex);
  const float danger = playerboard::PlayerInfoDanger
                           ? playerboard::PlayerInfoDanger->value
                           : 30.0f;
  // Old icon band: medkit under the danger line (but alive), dead -> skull
  // only (the two never showed together at health==0).
  const bool showMedkit = health > 0.0f && health < danger;
  const bool showDead = health <= 0.0f;
  const bool showCrouch =
      !showMedkit && !showDead && ent &&
      std::fabs(ent->curstate.maxs[2] - ent->curstate.mins[2]) < 64.0f;

  if (Rml::Element *icon = child(tile, "icon-crouch"))
    AppliedDom.property(icon, "display", showCrouch ? "block" : "none");
  if (Rml::Element *icon = child(tile, "icon-medkit"))
    AppliedDom.property(icon, "display", showMedkit ? "block" : "none");
  if (Rml::Element *icon = child(tile, "icon-dead"))
    AppliedDom.property(icon, "display", showDead ? "block" : "none");
  return true;
}

void PlayerBoardPanel::onData(double frameTime) {
  (void)frameTime;
  // The document must be shown for any tile to render; per-tile display
  // flips carry the real visibility (everything else runs in onRedraw -
  // the world projection phase).
  if (hudstate::isHudHidden(hudstate::kHideAll) || hudstate::isSpectate())
    hide();
  else
    show();
}

void PlayerBoardPanel::onRedraw(double frameTime) {
  (void)frameTime;
  // Old Think gate chain: cvar -> local player -> self -> entity state ->
  // range -> view cone; the pass-through survivor gets UpdateClientInfo.
  const bool enabled =
      !playerboard::PlayerInfo || playerboard::PlayerInfo->value > 0;
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (!enabled || !local || !Document ||
      hudstate::isHudHidden(hudstate::kHideAll) || hudstate::isSpectate()) {
    reset();
    return;
  }

  // All 32 tiles re-evaluated per frame (the old fleet's Think); hidden
  // tiles cost a display flip only.
  for (int index = 1; index <= playerres::kMaxPlayers; index++) {
    Rml::Element *tile = child(Document, tileId(index));
    if (!tile)
      continue;
    if (index == local->index) {
      AppliedDom.property(tile, "display", "none");
      continue;
    }

    const cl_entity_t *ent = gEngfuncs.GetEntityByIndex(index);
    if (!ent || ent->curstate.messagenum != local->curstate.messagenum ||
        !ent->player || !ent->model) {
      AppliedDom.property(tile, "display", "none");
      continue;
    }

    vec3_t toTarget{};
    float distanceSquared = 0.0f;
    for (int i = 0; i < 3; i++) {
      toTarget[i] = ent->curstate.origin[i] - local->curstate.origin[i];
      distanceSquared += toTarget[i] * toTarget[i];
    }
    if (distanceSquared >= kMaxDistance * kMaxDistance) {
      AppliedDom.property(tile, "display", "none");
      continue;
    }

    vec3_t view{};
    gEngfuncs.GetViewAngles(view);
    abc::CMathlib::AngleVectors(view, view, nullptr, nullptr);
    const float distance = std::sqrt(distanceSquared);
    float dot = 0.0f;
    for (int i = 0; i < 3; i++)
      dot += toTarget[i] * view[i];
    if (distance <= 0)
      continue;
    dot /= distance;
    if (dot <= kMinViewDot) {
      AppliedDom.property(tile, "display", "none");
      continue;
    }

    vec3_t anchor{};
    for (int i = 0; i < 3; i++)
      anchor[i] = ent->curstate.origin[i];
    anchor[2] += 45.0f;
    float screen[2] = {};
    if (framework::projection::worldToScreen(anchor, screen)) {
      AppliedDom.property(tile, "display", "none");
      continue;
    }

    AppliedDom.property(tile, "left",
                        std::to_string(screen[0] - kTileHalfWidth) + "px");
    AppliedDom.property(tile, "top", std::to_string(screen[1]) + "px");
    AppliedDom.property(tile, "display",
                        updateClientInfo(tile, index) ? "block" : "none");
  }
}

} // namespace panels
