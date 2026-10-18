#pragma once

// panels::GIndicator - grenade proximity indicator.
// from: ABCEnchance/src/hud/vgui/gindicator.* (GBK->UTF-8).
//
// The tracked-entity list lives in the gindicator namespace with file-static
// state (09 §6 hook-module shape): the spine's HUD_AddEntity chain calls
// onAddEntity, the panel consumes the list per frame in onData (project each
// live grenade with the front-facing dot gate, position its marker element).
// Markers are DOM children created/removed per entity (DeadMsg row pattern).
//
// D-17 deviation: the old sequence!=0 gate is dropped - SC 5.26 grenade
// entities carry sequence 0 for their whole flight, which disabled the old
// panel entirely (trace evidence in 08 §4.32).

#include "../../framework/HudPanel.h"

struct cl_entity_s;

namespace panels {

namespace gindicator {

// Old CGenadeIndicatorPanel ctor defaults (persisted contract - never rename).
void registerCvars();
// Spine seam (old Viewport::AddEntity -> GIndicator::AddEntity). Applies the
// gates (model name, studio type, messagenum, range) inline.
void onAddEntity(cl_entity_s *ent, const char *modelname);

} // namespace gindicator

class GIndicatorPanel : public framework::HudPanel {
public:
  GIndicatorPanel();

  void reset() override;

protected:
  void onDocumentReady() override;
  void onData(double frameTime) override;
  void onRedraw(double frameTime) override;
};

} // namespace panels
