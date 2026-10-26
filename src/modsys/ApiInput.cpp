// framework input section - the sdk view of the UI input router. See
// ApiTable.h for how it joins the module table.
#include "ApiTable.h"
#include "ApiSnapshot.h"
#include "rmlui/RmlUiModule.h"
#include "rmlui/TextInput.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include "ModuleResources.h"

#include <map>
#include <memory>
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
      : Callbacks(callbacks), Owner(module_resources::current()) {}

  const AbcUiInputCallbacks &callbacks() const {
    return Callbacks;
  }

  bool claimsKey(int down, int keynum) override {
    return module_resources::invoke(Owner, "input key claim",
                                    Callbacks.claimsKey, Callbacks.userdata,
                                    down ? 1 : 0, keynum) != 0;
  }
  bool claimsWheel() override {
    return module_resources::invoke(Owner, "input wheel claim",
                                    Callbacks.claimsWheel,
                                    Callbacks.userdata) != 0;
  }
  bool blocksGameplay() const override {
    return module_resources::invoke(Owner, "input gameplay claim",
                                    Callbacks.blocksGameplay,
                                    Callbacks.userdata) != 0;
  }
  void onClaimedKeyDown(int keynum) override {
    module_resources::invoke(Owner, "input key", Callbacks.onClaimedKeyDown,
                             Callbacks.userdata, keynum);
  }
  bool onClaimedWheelPulse(int keynum) override {
    return module_resources::invoke(Owner, "input wheel",
                                    Callbacks.onClaimedWheelPulse,
                                    Callbacks.userdata, keynum) != 0;
  }
  void onOwnershipLost() override {
    module_resources::invoke(Owner, "input ownership lost",
                             Callbacks.onOwnershipLost, Callbacks.userdata);
  }

private:
  AbcUiInputCallbacks Callbacks{};
  module_resources::OwnerHandle Owner;
};

struct TokenRecord {
  std::unique_ptr<ModuleOwner> owner;
  uiinput::Token token;
  module_resources::Registration lifetime;
};

std::map<AbcUiInputToken, TokenRecord> &tokens() {
  static std::map<AbcUiInputToken, TokenRecord> instance;
  return instance;
}

AbcUiInputToken NextToken = 1;

void apiReleaseToken(AbcUiInputToken token);

AbcUiInputToken apiTakeToken(const AbcUiInputCallbacks *callbacks) {
  if (!module_resources::accepting() ||
      !module_resources::contextRegistrationsOpen() || !callbacks ||
      rmlui::uiRouter().tearingDown() ||
      callbacks->structSize < sizeof(AbcUiInputCallbacks))
    return 0;
  std::unique_ptr<ModuleOwner> owner(new (std::nothrow)
                                         ModuleOwner(*callbacks));
  if (!owner)
    return 0;
  const AbcUiInputToken id = NextToken++;
  TokenRecord record;
  record.owner = std::move(owner);
  record.token = rmlui::uiRouter().acquire(record.owner.get(),
                                           uiinput::OwnerTier::kInteractiveHud);
  tokens()[id] = std::move(record);
  tokens()[id].lifetime.track([id] { apiReleaseToken(id); },
                              module_resources::Lifetime::Context);
  return id;
}

void apiReleaseToken(AbcUiInputToken token) {
  const auto itor = tokens().find(token);
  if (itor == tokens().end())
    return;
  auto closing = tokens().extract(itor);
  closing.mapped().token.release();
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

int32_t apiImeState(AbcUiImeState *out) {
  if (!out)
    return ABCUI_ERROR_INVALID;
  // Refresh the candidate page while a composition runs (the read is the
  // configuration-independent feed; see refreshCompositionCandidates).
  rmlui::refreshCompositionCandidates();
  AbcUiImeState state{};
  state.structSize = sizeof(state);
  auto *context = rmlui::context();
  auto *focus = context ? context->GetFocusElement() : nullptr;
  auto *document = focus ? focus->GetOwnerDocument() : nullptr;
  const auto owner = module_resources::current();
  // RmlUi reports source URLs with ':' escaped to '|' (StreamFile's
  // url-safe path), so a mod:// document answers "mod|//..." - match both
  // forms or every module editor would fail the ownership check.
  if (owner && document && document->IsVisible()) {
    const std::string url = document->GetSourceURL();
    if (url.starts_with("mod://" + owner->id + "/") ||
        url.starts_with("mod|//" + owner->id + "/"))
      rmlui::ime::snapshot(state);
  }
  abi::copySnapshot(out, state);
  return ABCUI_OK;
}

const AbcUiInputApi kInputApi = {
    sizeof(AbcUiInputApi), apiTakeToken,     apiReleaseToken,      apiTokenHeld,
    apiExclusive,          apiTextComposing, apiCancelComposition, apiMenuUp,
    apiImeLanguageTag,     apiImeState,
};

} // namespace

const AbcUiInputApi *get() {
  return &kInputApi;
}

} // namespace InputApi
} // namespace framework
