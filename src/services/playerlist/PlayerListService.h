#pragma once

// services::playerlist::access - the process-lifetime player-list model.
//
// The model, its roster seam and the voice mute backend belong to the
// framework (the composition root wires them once); the player-list page is
// a module and reads the model through the sdk playerlist section. This
// access point is the only framework-side handle the section uses; it is
// null until the composition root has assembled (never a second copy of the
// model).

namespace services {
namespace playerlist {

class PlayerListModel;

void setModel(PlayerListModel *model);
PlayerListModel *model();

} // namespace playerlist
} // namespace services
