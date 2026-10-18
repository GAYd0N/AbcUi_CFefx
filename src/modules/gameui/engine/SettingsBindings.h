#pragma once
// Internal SettingsBindings seam; no engine SDK exposed.
#include "services/settings/SettingsModel.h"
#include "services/settings/KeyBindModel.h"
#include "services/settings/VoiceModel.h"
namespace gameui::engine {
services::settings::SettingsModel::Seams settingsSeams();
services::settings::VoiceModel::Seams voiceSeams();
services::settings::KeyBindModel::Seams bindSeams();
} // namespace gameui::engine
