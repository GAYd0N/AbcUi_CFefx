// framework::ConnectionApi - the sdk connection section (the T14
// connection surfaces' data and intents). See ApiTable.h for how it joins
// the module table.
#include "ApiTable.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "../services/loading/ConnectionService.h"
#include "../services/loading/LoadingModel.h"

namespace framework {
namespace ConnectionApi {
namespace {

void copyText(const std::string &text, char *out, uint32_t outSize) {
  if (!out || outSize == 0)
    return;
  uint32_t i = 0;
  for (; i + 1 < outSize && i < text.size(); ++i)
    out[i] = text[i];
  out[i] = '\0';
}

const services::loading::LoadingModel *model() {
  return services::loading::connectionAccess().model;
}

uint32_t apiGeneration(void) {
  const auto *target = model();
  return target ? target->generation() : 0;
}

int32_t apiCanceled(void) {
  const auto *target = model();
  return target && target->canceled() ? 1 : 0;
}

int32_t apiAddress(char *out, uint32_t outSize) {
  const auto *target = model();
  if (!target)
    return 0;
  copyText(target->address(), out, outSize);
  return static_cast<int32_t>(target->address().size());
}

int32_t apiStatusText(char *out, uint32_t outSize) {
  const auto *target = model();
  if (!target)
    return 0;
  copyText(target->statusText(), out, outSize);
  return static_cast<int32_t>(target->statusText().size());
}

int32_t apiResourceText(char *out, uint32_t outSize) {
  const auto *target = model();
  if (!target)
    return 0;
  copyText(target->resourceText(), out, outSize);
  return static_cast<int32_t>(target->resourceText().size());
}

int32_t apiSecondaryText(char *out, uint32_t outSize) {
  const auto *target = model();
  if (!target)
    return 0;
  copyText(target->secondaryText(), out, outSize);
  return static_cast<int32_t>(target->secondaryText().size());
}

float apiSecondaryFraction(void) {
  const auto *target = model();
  return target ? target->secondaryFraction() : 0.0f;
}

int32_t apiPrimaryKnown(void) {
  const auto *target = model();
  return target && target->primary().Known ? 1 : 0;
}

float apiPrimaryFraction(void) {
  const auto *target = model();
  if (!target || !target->primary().Known)
    return 0.0f;
  const auto &primary = target->primary();
  if (primary.Fraction > 0.0f)
    return primary.Fraction;
  if (primary.Steps > 0)
    return static_cast<float>(primary.Point) /
           static_cast<float>(primary.Steps);
  return 0.0f;
}

int32_t apiErrorReason(char *out, uint32_t outSize) {
  const auto &seam = services::loading::connectionAccess();
  if (!seam.errorReason)
    return 0;
  const std::string reason = seam.errorReason();
  copyText(reason, out, outSize);
  return static_cast<int32_t>(reason.size());
}

int32_t apiPasswordInvalid(void) {
  const auto *target = model();
  // The per-generation prompt count is the authoritative signal: the
  // second prompt of a generation means the previous password was
  // rejected (the engine's own flag polarity is not pinned).
  return target && target->passwordPrompts() >= 2 ? 1 : 0;
}

int32_t apiCancelLoading(void) {
  const auto &seam = services::loading::connectionAccess();
  return seam.cancelLoading && seam.cancelLoading() ? 1 : 0;
}

void apiRetryFailed(void) {
  const auto &seam = services::loading::connectionAccess();
  if (seam.retryFailed)
    seam.retryFailed();
}

void apiReturnHome(void) {
  const auto &seam = services::loading::connectionAccess();
  if (seam.returnHome)
    seam.returnHome();
}

int32_t apiSubmitPassword(const char *password) {
  const auto &seam = services::loading::connectionAccess();
  if (!seam.submitPassword || !password || !*password)
    return 0;
  return seam.submitPassword(password) ? 1 : 0;
}

void apiPasswordCancel(void) {
  const auto &seam = services::loading::connectionAccess();
  if (seam.passwordCancel)
    seam.passwordCancel();
}

int32_t apiConnectAddress(const char *address, char *error,
                          uint32_t errorSize) {
  const auto &seam = services::loading::connectionAccess();
  if (!seam.connectAddress || !address)
    return 0;
  std::string errorToken;
  const bool ok = seam.connectAddress(address, errorToken);
  if (!ok && error && errorSize > 0)
    copyText(errorToken, error, errorSize);
  return ok ? 1 : 0;
}

const AbcUiConnectionApi kConnectionApi = {
    sizeof(AbcUiConnectionApi),
    apiGeneration,
    apiCanceled,
    apiAddress,
    apiStatusText,
    apiResourceText,
    apiSecondaryText,
    apiSecondaryFraction,
    apiPrimaryKnown,
    apiPrimaryFraction,
    apiErrorReason,
    apiPasswordInvalid,
    apiCancelLoading,
    apiRetryFailed,
    apiReturnHome,
    apiSubmitPassword,
    apiPasswordCancel,
    apiConnectAddress,
};

} // namespace

const AbcUiConnectionApi *get() {
  return &kConnectionApi;
}

} // namespace ConnectionApi
} // namespace framework
