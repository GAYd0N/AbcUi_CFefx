#include <abcui/module_api.h>

#include <map>

namespace {

const AbcUiModuleApi *Api = nullptr;
struct Bindings {
  uint64_t retry = 0;
  uint64_t quit = 0;
};

// MSVC's guarded local-static initialization introduces a TLS directory,
// which manually mapped modules cannot use. The module CRT owns this state.
std::map<AbcUiDocument *, Bindings> BindingRecords;

int32_t ABCUI_CALL onRetry(void *userdata, AbcUiElement *,
                           const AbcUiElementEvent *) {
  Api->documents->requestRetry(static_cast<AbcUiDocument *>(userdata));
  return 1;
}

int32_t ABCUI_CALL onQuit(void *, AbcUiElement *, const AbcUiElementEvent *) {
  Api->menu->quitNow();
  return 1;
}

void ABCUI_CALL onReady(void *, AbcUiDocument *document) {
  Bindings bound;
  if (auto *element = Api->ui->querySelector(document, "emergency-retry"))
    bound.retry =
        Api->ui->elementAddListener(element, "click", onRetry, document);
  if (auto *element = Api->ui->querySelector(document, "emergency-quit"))
    bound.quit =
        Api->ui->elementAddListener(element, "click", onQuit, document);
  BindingRecords[document] = bound;
}

void ABCUI_CALL onClose(void *, AbcUiDocument *document) {
  const auto it = BindingRecords.find(document);
  if (it == BindingRecords.end())
    return;
  // Listener tokens remember their actual target, even after DOM teardown.
  Api->ui->elementRemoveListener(nullptr, "click", it->second.retry);
  Api->ui->elementRemoveListener(nullptr, "click", it->second.quit);
  BindingRecords.erase(it);
}

void ABCUI_CALL onLoad() {
  AbcUiRecoveryDesc desc{};
  desc.structSize = sizeof(desc);
  desc.path = "mod://abcui.recovery/rml/recovery.rml";
  desc.onReady = onReady;
  desc.onClose = onClose;
  if (!Api->documents->registerRecovery(&desc))
    Api->core->failInitialization("recovery provider registration refused");
}

} // namespace

ABCUI_MODULE_EXPORT int ABCUI_CALL
AbcUiModule_Main(uint32_t abiVersion, const AbcUiModuleApi *api,
                 AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api ||
      api->structSize < sizeof(AbcUiModuleApi) ||
      !ABCUI_API_HAS_MEMBER(AbcUiDocumentsApi, api->documents, requestRetry) ||
      !ABCUI_API_HAS_MEMBER(AbcUiMenuApi, api->menu, quitNow) ||
      !ABCUI_API_HAS_MEMBER(AbcUiCoreApi, api->core, failInitialization) ||
      !api->ui || !AbcUiModule_InitDescriptor(out))
    return 0;
  Api = api;
  out->id = "abcui.recovery";
  out->version = "2.0.0";
  out->onLoad = onLoad;
  return 1;
}
