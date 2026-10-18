// services::settings::VoiceModel implementation - see VoiceModel.h.
#include "VoiceModel.h"

#include <cmath>

namespace services {
namespace settings {

namespace {
constexpr float kVolumeEpsilon = 1e-4f;
constexpr float kVolumeMin = 0.0f;
constexpr float kVolumeMax = 1.0f;
} // namespace

void VoiceModel::snapshot() {
  Available_ =
      Seams_.tweakInterface != nullptr && Seams_.tweakInterface() != nullptr;
  if (!Available_) {
    // Fail-closed: no interface, no controls, no test session.
    if (Testing_)
      stopTest();
    MicVolume_ = 0.0f;
    MicBoost_ = false;
    MicVolumeDraft_ = 0.0f;
    MicBoostDraft_ = false;
    MicVolumeDirty_ = false;
    MicBoostDirty_ = false;
    return;
  }
  if (Seams_.getControl) {
    MicVolume_ = Seams_.getControl(0);        // MicrophoneVolume
    MicBoost_ = Seams_.getControl(2) != 0.0f; // MicBoost
  }
  // Fresh truth invalidates stale drafts (the device is the only truth).
  MicVolumeDraft_ = MicVolume_;
  MicBoostDraft_ = MicBoost_;
  MicVolumeDirty_ = false;
  MicBoostDirty_ = false;
}

void VoiceModel::setMicVolumeDraft(float value) {
  if (!Available_)
    return;
  if (!std::isfinite(value))
    return;
  MicVolumeDraft_ = value < kVolumeMin
                        ? kVolumeMin
                        : (value > kVolumeMax ? kVolumeMax : value);
  MicVolumeDirty_ = std::fabs(MicVolumeDraft_ - MicVolume_) > kVolumeEpsilon;
}

void VoiceModel::setMicBoostDraft(bool value) {
  if (!Available_)
    return;
  MicBoostDraft_ = value;
  MicBoostDirty_ = value != MicBoost_;
}

bool VoiceModel::micDirty() const {
  return MicVolumeDirty_;
}

bool VoiceModel::micBoostDirty() const {
  return MicBoostDirty_;
}

bool VoiceModel::applyMicVolume() {
  if (!Available_)
    return false; // no interface: the write never happened
  if (!MicVolumeDirty_)
    return true; // nothing to do is a clean success
  if (!Seams_.setControl || !Seams_.setControl(0, MicVolumeDraft_))
    return false;
  const float readback =
      Seams_.getControl ? Seams_.getControl(0) : MicVolumeDraft_;
  if (std::fabs(readback - MicVolumeDraft_) > kVolumeEpsilon)
    return false; // the device did not take the value: keep the draft
  MicVolume_ = readback;
  MicVolumeDraft_ = readback;
  MicVolumeDirty_ = false;
  return true;
}

bool VoiceModel::applyMicBoost() {
  if (!Available_)
    return false;
  if (!MicBoostDirty_)
    return true;
  if (!Seams_.setControl || !Seams_.setControl(2, MicBoostDraft_ ? 1.0f : 0.0f))
    return false;
  const bool readback =
      Seams_.getControl ? Seams_.getControl(2) != 0.0f : MicBoostDraft_;
  if (readback != MicBoostDraft_)
    return false;
  MicBoost_ = readback;
  MicBoostDraft_ = readback;
  MicBoostDirty_ = false;
  return true;
}

bool VoiceModel::startTest() {
  if (!Available_ || Testing_)
    return Testing_;
  if (!Seams_.startTest || Seams_.startTest() == 0)
    return false; // the engine refused (no device): the UI reports, nothing
                  // fakes
  Testing_ = true;
  return true;
}

void VoiceModel::stopTest() {
  if (!Testing_)
    return;
  if (Seams_.stopTest)
    Seams_.stopTest();
  Testing_ = false;
}

float VoiceModel::speakingLevel() const {
  if (!Testing_ || !Seams_.speakingVolume)
    return 0.0f;
  const int raw = Seams_.speakingVolume();
  if (raw <= 0)
    return 0.0f;
  const float level = static_cast<float>(raw) / 255.0f;
  return level > 1.0f ? 1.0f : level;
}

} // namespace settings
} // namespace services
