#pragma once

// services::settings::PageModels / PageAccess - the seam between the
// framework's settings models and the sdk settings section.
//
// The four models, the parsed page tree, the favorite store and the spray
// maker stay framework-side (the composition root owns them); the settings
// page is a module and reaches them through the section. This header is the
// framework-internal half:
//
//   PageModels  - the model pointers the section projects;
//   PageAccess  - the two edges the section cannot implement itself (the
//                 display-mode transaction's deferred restart ordering and
//                 the spray maker's decode/encode/write + texture upload);
//   the page generation - bumped on every framework-side change the section
//                 does not observe through its own calls (a captured key
//                 edge, a display-mode transition, the spray maker's answer),
//                 so a page can re-project on change instead of every frame;
//   the spray maker's last answer (the maker runs asynchronously through the
//                 composition root, the page reads it back).
//
// Per the AGENTS.md service rule the storage is a function-local static: the
// composition root writes it, the section reads it.

#include "KeyBindModel.h"
#include "ModelFavorites.h"
#include "SettingsModel.h"
#include "VideoModeModel.h"
#include "VoiceModel.h"

#include <functional>
#include <string>
#include <vector>

namespace services::uiconfig {
struct SettingsPageDefinition;
} // namespace services::uiconfig

namespace services::settings {

struct PageModels {
  SettingsModel *rows = nullptr;
  KeyBindModel *bind = nullptr;
  VideoModeModel *video = nullptr;
  VoiceModel *voice = nullptr;
  ModelFavorites *favorites = nullptr;
  const std::vector<uiconfig::SettingsPageDefinition> *pages = nullptr;
};

// Bound once by the composition root (the module object and the models are
// process-lifetime). The section tolerates a null binding: every call then
// reports "unavailable" instead of dereferencing.
void setPageModels(const PageModels &models);
const PageModels &pageModels();

struct PageAccess {
  // The display-mode transaction (persist the recovery record, write the
  // changed commands, restart). True when the batch was issued.
  std::function<bool()> videoApply;
  // The confirmation window's two outcomes (the flow also owns the deferred
  // restart ordering, so both land there).
  std::function<bool()> videoKeep;
  std::function<bool()> videoRevert;
  // The spray maker for one absolute image path. True when accepted.
  std::function<bool(const std::string &path)> sprayMake;
  // Starts a key-binding capture session: the model row plus the input token
  // the framework owns (the router's kCapture tier). True when the session is
  // live afterwards.
  std::function<bool(int32_t actionIndex)> beginBindCapture;
};

void setPageAccess(const PageAccess &access);
const PageAccess &pageAccess();

// The page generation (see the header note).
void bumpPageRevision();
uint32_t pageRevision();

// The spray maker's last answer (the maker's result sink lands here).
void setSprayResult(bool ok, const std::string &message,
                    const std::string &previewUri);
const std::string &sprayStatus();
const std::string &sprayPreviewUri();

// The player-model favorite generation (the picker's rebuild gate): the
// store keeps no counter of its own, so the section's writes bump this one.
void bumpFavoritesGeneration();
uint32_t favoritesGeneration();

} // namespace services::settings
