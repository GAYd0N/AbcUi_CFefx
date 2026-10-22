// framework::MenuApi - the sdk menu section (the replacement menu's
// presentation seam). See ApiTable.h for how it joins the module table.
#include "ApiTable.h"

#include <algorithm>
#include <cstring>

#include "../services/menu/MenuHost.h"

namespace framework {
namespace MenuApi {
namespace {

void apiState(AbcUiMenuState *out) {
  if (!out)
    return;
  AbcUiMenuState full{};
  services::menu::fill(full);
  // An older caller's shorter snapshot keeps its own tail untouched.
  uint32_t declared = out->structSize;
  if (declared == 0)
    declared = static_cast<uint32_t>(sizeof(full));
  std::memcpy(out, &full, std::min<size_t>(declared, sizeof(full)));
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

const AbcUiMenuApi kMenuApi = {
    sizeof(AbcUiMenuApi), apiState,         apiOpenPage, apiBack,
    apiEnsureUp,          apiResume,        apiQuit,     apiDisconnect,
    apiToggleConsole,     apiDirectConnect,
};

} // namespace

const AbcUiMenuApi *get() {
  return &kMenuApi;
}

} // namespace MenuApi
} // namespace framework
