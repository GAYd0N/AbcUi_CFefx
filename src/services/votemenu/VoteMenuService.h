#pragma once

// services::votemenu::access - the process-lifetime vote-creation model.
//
// The model, its roster seam and the server MapList feed belong to the
// framework (the composition root wires them once); the vote page is a
// module and reads the model through the sdk votemenu section. This access
// point is the only framework-side handle the section uses; it is null until
// the composition root has assembled (never a second copy of the model).

namespace services {
namespace votemenu {

class VoteMenuModel;

void setModel(VoteMenuModel *model);
VoteMenuModel *model();

} // namespace votemenu
} // namespace services
