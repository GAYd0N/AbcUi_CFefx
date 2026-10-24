// framework::MenuApi - the sdk menu section (the replacement menu's
// presentation seam). See ApiTable.h for how it joins the module table.
#include "ApiTable.h"
#include "ApiSnapshot.h"

#include <algorithm>
#include <cstring>

#include "../services/menu/MenuHost.h"
#include "../services/GameUiBridge.h"
#include "ModuleResources.h"

namespace framework {
namespace MenuApi {
namespace {

void apiState(AbcUiMenuState *out) {
  if (!out)
    return;
  AbcUiMenuState full{};
  services::menu::fill(full);
  abi::copySnapshot(out, full);
}

int32_t apiOpenPage(int32_t page) {
  const services::menu::Host &seam = services::menu::host();
  return seam.openPage ? seam.openPage(page) : 0;
}

void apiBack(void) {
  const services::menu::Host &seam = services::menu::host();
  if (seam.back)
    seam.back();
}

int32_t apiEnsureUp(void) {
  const services::menu::Host &seam = services::menu::host();
  return seam.ensureUp ? seam.ensureUp() : 0;
}

int32_t apiResume(void) {
  const services::menu::Host &seam = services::menu::host();
  return seam.resume ? seam.resume() : 0;
}

void apiQuit(void) {
  const services::menu::Host &seam = services::menu::host();
  if (seam.quit)
    seam.quit();
}

void apiQuitResolve(int32_t confirmed) {
  const services::menu::Host &seam = services::menu::host();
  if (seam.quitResolve)
    seam.quitResolve(confirmed);
}

void apiDisconnect(void) {
  const services::menu::Host &seam = services::menu::host();
  if (seam.disconnect)
    seam.disconnect();
}

void apiToggleConsole(void) {
  const services::menu::Host &seam = services::menu::host();
  if (seam.toggleConsole)
    seam.toggleConsole();
}

void apiDirectConnect(void) {
  const services::menu::Host &seam = services::menu::host();
  if (seam.directConnect)
    seam.directConnect();
}

void apiQuitNow() {
  const auto &seam = services::menu::host();
  if (seam.quitNow)
    seam.quitNow();
}

module_resources::Registration &takeoverLifetime() {
  static module_resources::Registration lifetime;
  return lifetime;
}

int32_t apiSetNativeTakeover(int32_t enabled) {
  const auto owner = module_resources::current();
  auto &lifetime = takeoverLifetime();
  if (!owner || !owner->initializing || !module_resources::accepting() ||
      (lifetime.tracked() && lifetime.owner() != owner))
    return 0;
  lifetime.forget();
  lifetime = module_resources::Registration{};
  services::nativeUiBridge().setTakeoverEnabled(enabled != 0);
  if (enabled)
    lifetime.track([] {
      services::nativeUiBridge().setTakeoverEnabled(false);
      takeoverLifetime().forget();
    });
  return 1;
}

int32_t apiDispatchCommand(const char *name) {
  const auto &seam = services::menu::host();
  return name && services::nativeUiBridge().armed() && seam.dispatchCommand
             ? seam.dispatchCommand(name)
             : 0;
}

const AbcUiMenuApi kMenuApi = {
    sizeof(AbcUiMenuApi),
    apiState,
    apiOpenPage,
    apiBack,
    apiEnsureUp,
    apiResume,
    apiQuit,
    apiDisconnect,
    apiToggleConsole,
    apiDirectConnect,
    apiQuitResolve,
    apiSetNativeTakeover,
    apiDispatchCommand,
    apiQuitNow,
};

} // namespace

const AbcUiMenuApi *get() {
  return &kMenuApi;
}

} // namespace MenuApi
} // namespace framework
