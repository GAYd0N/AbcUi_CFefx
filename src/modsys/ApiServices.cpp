// framework shell/voice sections - the sdk seams the interactive panels need
// (profile links, copy actions and the per-player voice block list). See
// ApiTable.h for how they join the module table.
#include "ApiTable.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/SystemInterface.h>

#include "../framework/ScoreSeat.h"
#include "../framework/ShellActions.h"
#include "../services/voice/VoiceStatus.h"

namespace framework {
namespace ScoreApi {
namespace {

void apiSetHeld(int32_t held) {
  scoreseat::setHeld(held != 0);
}

int32_t apiHeld() {
  return scoreseat::held() ? 1 : 0;
}

void apiSetVisible(int32_t visible) {
  scoreseat::setVisible(visible != 0);
}

int32_t apiPointerMode() {
  return scoreseat::pointerMode() ? 1 : 0;
}

void apiSetPointerMode(int32_t enabled) {
  scoreseat::setPointerMode(enabled != 0);
}

int32_t apiWantsWheel() {
  return scoreseat::wantsWheel() ? 1 : 0;
}

int32_t apiIntermission() {
  return scoreseat::intermission() ? 1 : 0;
}

const AbcUiScoreApi kScoreApi = {
    sizeof(AbcUiScoreApi), apiSetHeld,        apiHeld,       apiSetVisible,
    apiPointerMode,        apiSetPointerMode, apiWantsWheel, apiIntermission,
};

} // namespace

const AbcUiScoreApi *get() {
  return &kScoreApi;
}

} // namespace ScoreApi

namespace ShellApi {
namespace {

void apiOpenUrl(const char *url) {
  if (url && *url)
    OpenExternalUrl(url);
}

void apiCopyText(const char *text) {
  if (!text || !*text)
    return;
  if (Rml::SystemInterface *system = Rml::GetSystemInterface())
    system->SetClipboardText(text);
}

int32_t apiClipboardText(char *out, uint32_t outSize) {
  Rml::SystemInterface *system = Rml::GetSystemInterface();
  if (!system)
    return 0;
  Rml::String clipboard;
  system->GetClipboardText(clipboard);
  const std::string text = clipboard.c_str();
  if (out && outSize > 0) {
    uint32_t i = 0;
    for (; i + 1 < outSize && i < text.size(); ++i)
      out[i] = text[i];
    out[i] = '\0';
  }
  return static_cast<int32_t>(text.size());
}

const AbcUiShellApi kShellApi = {
    sizeof(AbcUiShellApi),
    apiOpenUrl,
    apiCopyText,
    apiClipboardText,
};

} // namespace

const AbcUiShellApi *get() {
  return &kShellApi;
}

} // namespace ShellApi

namespace VoiceApi {
namespace {

int32_t apiAvailable() {
  return services::voice::status().available() ? 1 : 0;
}

int32_t apiIsBlocked(int32_t index) {
  return services::voice::status().isPlayerBlocked(index) ? 1 : 0;
}

int32_t apiSetBlocked(int32_t index, int32_t blocked) {
  return services::voice::status().setPlayerBlockedState(index, blocked != 0)
             ? 1
             : 0;
}

const AbcUiVoiceApi kVoiceApi = {
    sizeof(AbcUiVoiceApi),
    apiAvailable,
    apiIsBlocked,
    apiSetBlocked,
};

} // namespace

const AbcUiVoiceApi *get() {
  return &kVoiceApi;
}

} // namespace VoiceApi
} // namespace framework
