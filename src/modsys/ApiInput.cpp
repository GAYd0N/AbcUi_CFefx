// framework input section - the sdk view of the UI input router. See
// ApiTable.h for how it joins the module table.
#include "ApiTable.h"

#include <map>
#include <new>
#include <string>

#include "../framework/UiInputRouter.h"
#include "../rmlui/ImeOverlay.h"
#include "../rmlui/InputBridge.h"
#include "../rmlui/TextInput.h"
#include "../services/GameUiBridge.h"

namespace framework {
namespace InputApi {
namespace {

// One module-owned claim on the router: the adapter translates the C
// callbacks into the uiinput::Owner contract.
class ModuleOwner final : public uiinput::Owner {
public:
  explicit ModuleOwner(const AbcUiInputCallbacks &callbacks)
      : Callbacks(callbacks) {}

  const AbcUiInputCallbacks &callbacks() const {
    return Callbacks;
  }

  bool claimsKey(int down, int keynum) override {
    return Callbacks.claimsKey &&
           Callbacks.claimsKey(Callbacks.userdata, down ? 1 : 0, keynum) != 0;
  }
  bool claimsWheel() override {
    return Callbacks.claimsWheel &&
           Callbacks.claimsWheel(Callbacks.userdata) != 0;
  }
  bool blocksGameplay() const override {
    return Callbacks.blocksGameplay &&
           Callbacks.blocksGameplay(Callbacks.userdata) != 0;
  }
  void onClaimedKeyDown(int keynum) override {
    if (Callbacks.onClaimedKeyDown)
      Callbacks.onClaimedKeyDown(Callbacks.userdata, keynum);
  }
  bool onClaimedWheelPulse(int keynum) override {
    return Callbacks.onClaimedWheelPulse &&
           Callbacks.onClaimedWheelPulse(Callbacks.userdata, keynum) != 0;
  }
  void onOwnershipLost() override {
    if (Callbacks.onOwnershipLost)
      Callbacks.onOwnershipLost(Callbacks.userdata);
  }

private:
  AbcUiInputCallbacks Callbacks{};
};

struct TokenRecord {
  ModuleOwner *owner = nullptr;
  uiinput::Token token;
};

std::map<AbcUiInputToken, TokenRecord> &tokens() {
  static std::map<AbcUiInputToken, TokenRecord> instance;
  return instance;
}

AbcUiInputToken NextToken = 1;

AbcUiInputToken apiTakeToken(const AbcUiInputCallbacks *callbacks) {
  if (!callbacks || callbacks->structSize < sizeof(AbcUiInputCallbacks))
    return 0;
  auto *owner = new (std::nothrow) ModuleOwner(*callbacks);
  if (!owner)
    return 0;
  const AbcUiInputToken id = NextToken++;
  TokenRecord record;
  record.owner = owner;
  record.token =
      rmlui::uiRouter().acquire(owner, uiinput::OwnerTier::kInteractiveHud);
  tokens()[id] = std::move(record);
  return id;
}

void apiReleaseToken(AbcUiInputToken token) {
  const auto itor = tokens().find(token);
  if (itor == tokens().end())
    return;
  itor->second.token.release();
  delete itor->second.owner;
  tokens().erase(itor);
}

int32_t apiTokenHeld(AbcUiInputToken token) {
  const auto itor = tokens().find(token);
  return itor != tokens().end() && itor->second.token.held() ? 1 : 0;
}

int32_t apiExclusive() {
  return rmlui::uiRouter().exclusive() ? 1 : 0;
}

int32_t apiTextComposing() {
  return rmlui::textComposing() ? 1 : 0;
}

void apiCancelComposition() {
  rmlui::cancelComposition();
}

int32_t apiMenuUp() {
  return services::nativeUiBridge().menuUp() ? 1 : 0;
}

int32_t apiImeLanguageTag(char *out, uint32_t outSize) {
  const std::string tag = rmlui::ime::inputLanguageTag();
  if (out && outSize > 0) {
    uint32_t i = 0;
    for (; i + 1 < outSize && i < tag.size(); ++i)
      out[i] = tag[i];
    out[i] = 0;
  }
  return static_cast<int32_t>(tag.size());
}

const AbcUiInputApi kInputApi = {
    sizeof(AbcUiInputApi), apiTakeToken, apiReleaseToken,
    apiTokenHeld,          apiExclusive, apiTextComposing,
    apiCancelComposition,  apiMenuUp,    apiImeLanguageTag,
};

} // namespace

const AbcUiInputApi *get() {
  return &kInputApi;
}

} // namespace InputApi
} // namespace framework
