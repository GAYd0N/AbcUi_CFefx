// framework::DocumentsApi - the sdk documents section (module-owned
// interactive pages). See ApiTable.h for how it joins the module table.
#include "ApiTable.h"

#include <algorithm>
#include <cstring>

#include "ModuleDocuments.h"

namespace framework {
namespace DocumentsApi {
namespace {

AbcUiDocument *apiCreate(const AbcUiDocumentDesc *desc) {
  if (!desc || desc->structSize < offsetof(AbcUiDocumentDesc, flags) ||
      (desc->structSize < sizeof(AbcUiDocumentDesc) &&
       desc->structSize % sizeof(uint32_t) != 0))
    return nullptr;
  // Honor the caller's declared size (an older module's shorter descriptor
  // must not be read past its end); the missing tail stays zeroed. The name
  // and path are mandatory; never copy half a pointer or callback.
  AbcUiDocumentDesc full{};
  std::memcpy(&full, desc,
              std::min<size_t>(desc->structSize, sizeof(AbcUiDocumentDesc)));
  return module_documents::create(full);
}

void apiDestroy(AbcUiDocument *doc) {
  module_documents::destroy(doc);
}

int32_t apiLoaded(AbcUiDocument *doc) {
  Rml::ElementDocument *document = module_documents::documentOf(doc);
  return document ? 1 : 0;
}

int32_t apiDegraded(AbcUiDocument *doc) {
  return module_documents::degraded(doc) ? 1 : 0;
}

int32_t apiVisible(AbcUiDocument *doc) {
  return module_documents::visible(doc) ? 1 : 0;
}

void apiShow(AbcUiDocument *doc) {
  module_documents::show(doc);
}

void apiHide(AbcUiDocument *doc) {
  module_documents::hide(doc);
}

AbcUiElement *apiRoot(AbcUiDocument *doc) {
  return reinterpret_cast<AbcUiElement *>(module_documents::documentOf(doc));
}

int32_t apiRetry(AbcUiDocument *doc) {
  return module_documents::retry(doc);
}

const AbcUiDocumentsApi kDocumentsApi = {
    sizeof(AbcUiDocumentsApi),
    apiCreate,
    apiDestroy,
    apiLoaded,
    apiDegraded,
    apiVisible,
    apiShow,
    apiHide,
    apiRoot,
    apiRetry,
    module_documents::registerRecovery,
    module_documents::unregisterRecovery,
    module_documents::requestRetry,
};

} // namespace

const AbcUiDocumentsApi *get() {
  return &kDocumentsApi;
}

} // namespace DocumentsApi
} // namespace framework
