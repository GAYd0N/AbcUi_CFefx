// Engine seams extracted from GameUiModule.cpp; no page ownership.
#include <abc/CvarCmd.h>
#include <abc/Plugin.h>
#include <ivoicetweak.h>
#include "rmlui/VGUI2ExtensionImport.h"
#include "modules/gameui/engine/SettingsBindings.h"
#include "rmlui/modelpreview/Options.h"
#include <string_view>
namespace gameui::engine {
services::settings::SettingsModel::Seams settingsSeams() {
  services::settings::SettingsModel::Seams seams;
  seams.exists = [](const char *name) {
    return abc::CVAR_GET_POINTER(name) != nullptr;
  };
  seams.readFloat = [](const char *name) { return abc::CVAR_GET_FLOAT(name); };
  seams.writeFloat = [](const char *name, float value) {
    if (!abc::CVAR_GET_POINTER(name))
      return false;
    abc::CVAR_SET_FLOAT(name, value);
    return true;
  };
  // T13 string rows: real engine cvar string writes (the original page
  // sent `name "%s"` / `model %s` commands; the model validated tokens
  // BEFORE either path - and the direct API keeps user text out of the
  // command channel entirely).
  seams.writeString = [](const char *name, const char *value) {
    if (!abc::CVAR_GET_POINTER(name) || !value)
      return false;
    abc::CVAR_SET_STRING(name, value);
    return true;
  };
  seams.readString = [](const char *name) -> const char * {
    const cvar_t *cvar = abc::CVAR_GET_POINTER(name);
    return cvar ? cvar->string : nullptr;
  };
  seams.validateText = [](const char *name, const char *value) {
    return std::string_view(name) != "abc_ui_menu_weapon_model" || !*value ||
           !modelpreview::resolveWeaponModelPath(value).empty();
  };
  // T13 command toggles (in_mlook/in_jlook): IGameUIFuncs::IsKeyDown for
  // the truth, the engine command channel for +mlook/-mlook (buffered -
  // the model's kQueued settle covers the flush beat).
  seams.isKeyDown = [](const char *stateName) {
    return vgui2ext::isKeyDown(stateName);
  };
  seams.clientCmd = [](const char *command) {
    if (!command)
      return false;
    abc::EngineClientCmd(command);
    return true;
  };
  return seams;
}

services::settings::VoiceModel::Seams voiceSeams() {
  services::settings::VoiceModel::Seams seams;
  seams.tweakInterface = []() -> void * { return gEngfuncs.pVoiceTweak; };
  seams.startTest = []() -> int {
    return gEngfuncs.pVoiceTweak ? gEngfuncs.pVoiceTweak->StartVoiceTweakMode()
                                 : 0;
  };
  seams.stopTest = []() {
    if (gEngfuncs.pVoiceTweak)
      gEngfuncs.pVoiceTweak->EndVoiceTweakMode();
  };
  seams.getControl = [](int which) -> float {
    return gEngfuncs.pVoiceTweak ? gEngfuncs.pVoiceTweak->GetControlFloat(
                                       static_cast<VoiceTweakControl>(which))
                                 : 0.0f;
  };
  seams.setControl = [](int which, float value) -> bool {
    if (!gEngfuncs.pVoiceTweak)
      return false;
    gEngfuncs.pVoiceTweak->SetControlFloat(
        static_cast<VoiceTweakControl>(which), value);
    return true;
  };
  seams.speakingVolume = []() -> int {
    return gEngfuncs.pVoiceTweak ? gEngfuncs.pVoiceTweak->GetSpeakingVolume()
                                 : 0;
  };
  return seams;
}

services::settings::KeyBindModel::Seams bindSeams() {
  services::settings::KeyBindModel::Seams seams;
  seams.bindingForKey = [](int keynum) -> const char * {
    return vgui2ext::keyBindingForKey(keynum);
  };
  seams.keyNameForKey = [](int keynum) -> const char * {
    return vgui2ext::keyNameForKey(keynum);
  };
  seams.keyStringToKeyNum = [](const char *name) {
    return vgui2ext::keyStringToKeyNum(name);
  };
  seams.clientCmd = [](const char *command) {
    abc::EngineClientCmd(command);
    return true; // handed to the engine buffer; truth rides the re-read
  };
  return seams;
}
} // namespace gameui::engine
