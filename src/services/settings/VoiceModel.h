#pragma once

// services::settings::VoiceModel - the voice page's hardware rows (T13b).
//
// The shipped GameUI voice page drives two surfaces: cvars (voice_scale /
// voice_modenable - those live in SettingsModel's table) and the engine
// IVoiceTweak interface (gEngfuncs.pVoiceTweak - cl_enginefunc slot, filled
// by hw.dll): MicrophoneVolume(0) and MicBoost(2) control floats and the
// StartVoiceTweakMode/EndVoiceTweakMode microphone-test session with
// GetSpeakingVolume as the live input level. Evidence: COptionsSubVoice ctor
// (GameUI.dll FUN_1001dd90 - pVoiceTweak = engine slot +0x15c, controls,
// GetControlFloat(0)*100 slider init, GetControlFloat(2) boost state,
// whole-page disable when the interface is null) and the PageHide handler
// (LAB_1001e340 - the test ends when the page goes away).
//
// The interface pointer is read at runtime through the injected seam and a
// null pointer fail-closes the whole section (the original disables every
// control; we render the section unavailable - never a greyed pretend page).
//
// Resource pairing: the test session is a REAL device resource. It is
// started once per request and stopped on every exit path - explicit stop,
// page leave, menu focus loss (module onOwnershipLost), module shutdown -
// the same guarantee the original PageHide gives, extended to the paths the
// original never handled. The level readout is GetSpeakingVolume truth, not
// an animation.
//
// No engine includes: the seams keep the model offline-testable.

namespace services {
namespace settings {

class VoiceModel {
public:
  struct Seams {
    // All null-safe: a null tweakInterface fails the section closed.
    void *(*tweakInterface)() = nullptr; // pVoiceTweak
    int (*startTest)() = nullptr;        // StartVoiceTweakMode (0 on error)
    void (*stopTest)() = nullptr;        // EndVoiceTweakMode
    float (*getControl)(int which) = nullptr;             // GetControlFloat
    bool (*setControl)(int which, float value) = nullptr; // SetControlFloat
    int (*speakingVolume)() = nullptr; // GetSpeakingVolume 0..255
  };

  void setSeams(const Seams &seams) {
    Seams_ = seams;
  }

  // Reads the interface availability and the current control floats. A
  // null interface leaves the section unavailable (kUnavailable).
  void snapshot();

  bool available() const {
    return Available_;
  }

  // Mic transmit volume, engine scale 0..1 (the original page displays
  // x100; the page maps, the model keeps engine units).
  float micVolume() const {
    return MicVolume_;
  }
  // Boost state (0/1).
  bool micBoost() const {
    return MicBoost_;
  }

  // Draft edits (nothing reaches the device until apply()).
  void setMicVolumeDraft(float value);
  void setMicBoostDraft(bool value);
  float micVolumeDraft() const {
    return MicVolumeDraft_;
  }
  bool micBoostDraft() const {
    return MicBoostDraft_;
  }
  bool micDirty() const;
  bool micBoostDirty() const;
  bool isDirty() const {
    return micDirty() || micBoostDirty();
  }

  // Apply = SetControlFloat + read-back. A refused write or a disagreeing
  // read-back keeps the draft and returns false (the page reports the
  // failure; no fake success). The device floats carry NO persistence -
  // they are hardware state by definition.
  bool applyMicVolume();
  bool applyMicBoost();

  // Microphone test session: one start, guaranteed stop. start() fails
  // when the interface is missing or the engine refuses the mode; the
  // level is 0 while not testing.
  bool startTest();
  void stopTest();
  bool testing() const {
    return Testing_;
  }
  // Live input level 0..1 from GetSpeakingVolume (0..255). Not animated,
  // not sampled while the session is down - the raw engine answer.
  float speakingLevel() const;

private:
  Seams Seams_;
  bool Available_ = false;
  float MicVolume_ = 0.0f;
  bool MicBoost_ = false;
  float MicVolumeDraft_ = 0.0f;
  bool MicBoostDraft_ = false;
  bool MicVolumeDirty_ = false;
  bool MicBoostDirty_ = false;
  bool Testing_ = false;
};

} // namespace settings
} // namespace services
