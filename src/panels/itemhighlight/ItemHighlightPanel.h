#pragma once

// panels::ItemHighlight - world-item callouts (name markers + the pickup
// prompt). from: ABCEnchance/src/hud/vgui/itemhighlight.* (GBK->UTF-8).
//
// D-19 (P7 user directive): the highlight is delivered as pure HUD elements.
// The old codebase's model-shell era (TEMPENTITY "item_highlight.mdl" copies
// drawn around the item) is not ported; the repo's own final form - one
// CItemNamePanel per look-at item (corner-bracket icon + colored name) plus
// the centered pickup notice - maps 1:1 onto Rml: one marker <div> per
// tracked entity under #markers (DeadMsg row pattern) and the static #pickup
// notice, all positioned from the per-frame world projection.
//
// The tracked-entity list lives in the itemhighlight namespace with
// file-static state (09 §6 hook-module shape): the spine's HUD_AddEntity
// chain calls onAddEntity (old Viewport::AddEntity routing), the panel
// consumes it in onRedraw (old OnThink; the projection needs the render
// phase - 08 §4.31).
//
// Old-quirk ledger (D-17 family, all recorded in D-19):
// - hud_highlight_item_name now actually gates the name markers (old code
//   read it only in the both-off early return, so it was dead).
// - hud_highlight_item 0 hides existing markers immediately (old AddEntity
//   gate let already-tracked items keep painting).
// - the pickup prompt hides when the aim item walks out of the 86u reach
//   (old code only hid it when the aim cone broke - it stuck at distance).
// - a malformed item list warns and drops the trailing record (old
//   SYS_ERROR'd the client).
// - entity-index reuse re-assigns the tracked item (old emplace kept the
//   stale record until the messagenum sweep caught it a frame later).

#include "../../framework/HudPanel.h"
#include "../../framework/DomState.h"

#include <string>
#include <unordered_map>

struct cl_entity_s;

namespace panels {

namespace itemhighlight {

// Old HUD_Init-time cvar registration (persisted names - never rename).
void registerCvars();

// Spine seam (old Viewport::AddEntity -> CItemHighLightPanel::AddEntity):
// tracks studio-model entities whose modelindex is on the highlight list.
void onAddEntity(cl_entity_s *ent, const char *modelname);

#ifndef NDEBUG
// abc_ui_probe aid: tracked-entity count (the AddEntity chain's ground truth).
int debugTrackedCount();
#endif

} // namespace itemhighlight

class ItemHighlightPanel : public framework::HudPanel {
public:
  ItemHighlightPanel();

  void reset() override;

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;
  void onRedraw(double frameTime) override;

private:
  framework::DomState AppliedDom;
  // Per-marker DOM handles (kept from creation - a lookup by entindex would
  // collide with Rml's document-scoped GetElementById, 08 §4.33).
  struct Marker {
    Rml::Element *box = nullptr;
    Rml::Element *art = nullptr;
    Rml::Element *artAim = nullptr;
    Rml::Element *label = nullptr;
  };

  // Ensures/updates the marker for one tracked entity; returns it (null on
  // document-element failure). `screen` is the projected world position.
  Marker *updateMarker(int entindex, int itemIndex, float ratio,
                       const float screen[2]);
  void removeMarker(int entindex);
  void clearMarkers();

  // Old CItemPickupPanel::UpdateName: template + USE binding + item name.
  void updatePromptText(const char *nameToken);

  std::unordered_map<int, Marker> Markers;
  bool PromptShown = false;
};

} // namespace panels
