// services::settings page seam implementation - see the header.
#include "SettingsPageService.h"

namespace services::settings {
namespace {

PageModels &models() {
  static PageModels instance;
  return instance;
}

PageAccess &access() {
  static PageAccess instance;
  return instance;
}

struct PageState {
  uint32_t Revision = 1;
  uint32_t FavoriteGeneration = 1;
  std::string SprayStatus;
  std::string SprayPreviewUri;
};

PageState &state() {
  static PageState instance;
  return instance;
}

} // namespace

void setPageModels(const PageModels &value) {
  models() = value;
}

const PageModels &pageModels() {
  return models();
}

void setPageAccess(const PageAccess &value) {
  access() = value;
}

const PageAccess &pageAccess() {
  return access();
}

void bumpPageRevision() {
  ++state().Revision;
}

uint32_t pageRevision() {
  return state().Revision;
}

void setSprayResult([[maybe_unused]] bool ok, const std::string &message,
                    const std::string &previewUri) {
  state().SprayStatus = message;
  state().SprayPreviewUri = previewUri;
  bumpPageRevision();
}

const std::string &sprayStatus() {
  return state().SprayStatus;
}

const std::string &sprayPreviewUri() {
  return state().SprayPreviewUri;
}

void bumpFavoritesGeneration() {
  ++state().FavoriteGeneration;
  bumpPageRevision();
}

uint32_t favoritesGeneration() {
  return state().FavoriteGeneration;
}

} // namespace services::settings
