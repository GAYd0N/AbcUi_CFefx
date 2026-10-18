// services::loading::LoadingModel implementation - see LoadingModel.h.
#include "LoadingModel.h"

#include <abc/Parse.h>

#include <cstring>

namespace services {
namespace loading {

namespace {

// ASCII host/address charset: letters, digits, dot, dash, underscore.
// Everything else (spaces, quotes, semicolons, control bytes - the command
// channel separators) is refused: the address must never act as anything
// but one command payload.
bool validHostChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
}

bool parsePort(const char *text, int &outPort) {
  if (!text || !*text)
    return false;
  const std::string_view View(text);
  if (View.find_first_not_of("0123456789") != View.npos)
    return false;
  const auto Value = abc::ParseInt(View);
  if (!Value || *Value < 1 || *Value > 65535)
    return false;
  outPort = *Value;
  return true;
}

} // namespace

bool LoadingModel::parseAddress(const char *address, std::string &outHost,
                                int &outPort) {
  outHost.clear();
  outPort = 0;
  if (!address || !*address)
    return false;
  const char *colon = nullptr;
  for (const char *it = address; *it; ++it) {
    if (*it == ':') {
      if (colon)
        return false; // only one port separator
      colon = it;
    }
  }
  const size_t hostLength =
      colon ? static_cast<size_t>(colon - address) : std::strlen(address);
  if (hostLength == 0 || hostLength > 253)
    return false;
  for (size_t i = 0; i < hostLength; ++i) {
    if (!validHostChar(address[i]))
      return false;
  }
  outHost.assign(address, hostLength);
  if (colon && !parsePort(colon + 1, outPort))
    return false;
  return true;
}

void LoadingModel::startAttempt(uint32_t generation, const char *address) {
  Generation = generation;
  Address.assign(address ? address : "");
  Canceled = false;
  LevelStarted = false;
  LevelFinished = false;
  Primary = LoadingProgress{};
  SecondaryFraction = 0.0f;
  StatusText.clear();
  SecondaryText.clear();
  ResourceText.clear();
}

void LoadingModel::retire() {
  Generation = 0;
  // Canceled/latches reset with the next attempt; the address deliberately
  // survives so the failure page's retry can reuse it.
  Canceled = false;
}

void LoadingModel::loadingStarted(uint32_t generation, const char *resourceType,
                                  const char *resourceName) {
  if (!live(generation))
    return;
  if (resourceName && *resourceName)
    ResourceText.assign(resourceName);
  else if (resourceType && *resourceType)
    ResourceText.assign(resourceType);
  if (resourceType && std::string(resourceType) == "level")
    LevelStarted = true;
}

void LoadingModel::loadingFinished(uint32_t generation,
                                   const char *resourceType) {
  if (!live(generation))
    return;
  // Record only: whether the attempt becomes "in game" is the module's
  // level-fact + controller decision (T14 step a.3 - never assumed here).
  if (resourceType && std::string(resourceType) == "level")
    LevelFinished = true;
}

void LoadingModel::startProgress(uint32_t generation, const char *progressType,
                                 int steps) {
  if (!live(generation))
    return;
  (void)progressType; // the type string is not projected; steps carry the truth
  Primary.Point = 0;
  Primary.Steps = steps > 0 ? steps : 0;
  Primary.Known = steps > 0;
  Primary.Fraction = 0.0f;
}

void LoadingModel::continueProgress(uint32_t generation, int point,
                                    float fraction) {
  if (!live(generation))
    return;
  if (point > 0)
    Primary.Point = point;
  if (fraction > 1.0f)
    fraction = 1.0f;
  if (fraction < 0.0f)
    fraction = 0.0f;
  Primary.Fraction = fraction;
  // Known once the engine gave a usable total OR a positive fraction; an
  // unknown-total stream (steps<=0, fraction 0) stays unknown on purpose.
  if (Primary.Steps > 0 || fraction > 0.0f)
    Primary.Known = true;
}

void LoadingModel::stopProgress(uint32_t generation, bool error) {
  if (!live(generation))
    return;
  // Non-error stop = the phase's progress completed (the level load that
  // follows keeps the page up until LoadingFinished). Error handling is the
  // module's: it dispatches the failed outcome with the engine reason, and
  // the controller owns the text - no second copy here.
  (void)error;
}

void LoadingModel::setStatusText(uint32_t generation, const char *text) {
  if (!live(generation))
    return;
  StatusText.assign(text ? text : "");
}

void LoadingModel::setSecondaryProgress(uint32_t generation, float fraction) {
  if (!live(generation))
    return;
  if (fraction > 1.0f)
    fraction = 1.0f;
  if (fraction < 0.0f)
    fraction = 0.0f;
  SecondaryFraction = fraction;
}

void LoadingModel::setSecondaryProgressText(uint32_t generation,
                                            const char *text) {
  if (!live(generation))
    return;
  SecondaryText.assign(text ? text : "");
}

bool LoadingModel::requestCancel(uint32_t generation) {
  if (!live(generation))
    return false;
  if (Canceled)
    return false;
  Canceled = true;
  return true;
}

} // namespace loading
} // namespace services
