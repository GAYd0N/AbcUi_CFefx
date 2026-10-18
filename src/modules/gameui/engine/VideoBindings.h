#pragma once
// Internal VideoBindings seam; no engine SDK exposed.
#include "services/settings/VideoModeModel.h"
namespace gameui::engine {
// Deferred engine writes survive HUD context teardown. BackgroundWork is
// the sole consumer; neither page visibility nor focus owns this queue.
extern bool DeferredVideoRestart;
extern bool DeferredVideoConfirmRaise;
services::settings::VideoModeModel::Seams videoSeams();
} // namespace gameui::engine
