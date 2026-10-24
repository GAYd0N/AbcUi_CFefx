// framework::module_documents implementation - see ModuleDocuments.h.
#include "ModuleDocuments.h"
#include "ModuleResources.h"
#include "ModuleVfs.h"

#include <abc/Console.h>

#include <RmlUi/Core/Context.h>

#include <cstring>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "../framework/InteractiveDocument.h"

namespace framework {
namespace module_documents {
namespace {

struct RecoveryProvider {
  uint64_t id = 0;
  std::string path;
  AbcUiRecoveryDesc desc{};
  module_resources::Registration lifetime;
};

std::shared_ptr<RecoveryProvider> &recoveryProvider() {
  static std::shared_ptr<RecoveryProvider> instance;
  return instance;
}

class ModuleDocument final
    : public InteractiveDocument,
      public std::enable_shared_from_this<ModuleDocument> {
public:
  explicit ModuleDocument(const AbcUiDocumentDesc &desc)
      : InteractiveDocument(desc.name, desc.path,
                            (desc.flags & ABCUI_DOC_FLAG_BACKDROP) != 0,
                            desc.zIndex),
        Desc(desc) {}

  // Show/hide report transitions only: a repeated sync must not re-dispatch
  // page callbacks (InteractiveDocument::show/hide are idempotent for the
  // same reason).
  void showDocument() {
    auto keepAlive = shared_from_this();
    if (isVisible())
      return;
    if ((Desc.flags & ABCUI_DOC_FLAG_DECORATION) && document()) {
      document()->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
      document()->PullToFront();
    } else {
      InteractiveDocument::show();
    }
    module_resources::invoke(Lifetime.owner(), "document show", Desc.onShow,
                             Desc.userdata);
  }

  void hideDocument() {
    auto keepAlive = shared_from_this();
    if (!isVisible())
      return;
    // Before the DOM hides: a page closing its own composition/popups needs
    // the still-visible document.
    module_resources::invoke(Lifetime.owner(), "document hide", Desc.onHide,
                             Desc.userdata);
    InteractiveDocument::hide();
  }

  void closeDocument() {
    if (!isLoaded() || Closing)
      return;
    auto keepAlive = shared_from_this();
    Closing = true;
    module_resources::invoke(Lifetime.owner(), "document close", Desc.onClose,
                             Desc.userdata);
    close();
    Closing = false;
  }

  bool menuLayer() const {
    return (Desc.flags & ABCUI_DOC_FLAG_MENU) != 0;
  }

  bool usesRecovery(const std::shared_ptr<RecoveryProvider> &provider) const {
    return Recovery == provider;
  }

  bool mayRetry() const {
    const auto current = module_resources::current();
    return current == Lifetime.owner() ||
           (Recovery && current == Recovery->lifetime.owner());
  }

protected:
  void onDocumentReady() override {
    if ((Desc.flags & ABCUI_DOC_FLAG_DECORATION) && document())
      document()->SetProperty("pointer-events", "none");
    // Never fires for the built-in emergency page (degraded() reports it):
    // the page's ids do not exist there.
    module_resources::invoke(Lifetime.owner(), "document ready", Desc.onReady,
                             Desc.userdata);
  }

  const char *recoveryPath() override {
    Recovery = recoveryProvider();
    if (!Recovery || !module_resources::enabled(Recovery->lifetime.owner()))
      return nullptr;
    return Recovery->path.c_str();
  }

  void onRecoveryReady() override {
    const auto provider = Recovery;
    if (provider)
      module_resources::invoke(provider->lifetime.owner(), "recovery ready",
                               provider->desc.onReady, provider->desc.userdata,
                               reinterpret_cast<AbcUiDocument *>(this));
  }

  void onRecoveryClose() override {
    const auto provider = std::move(Recovery);
    if (provider)
      module_resources::invoke(provider->lifetime.owner(), "recovery close",
                               provider->desc.onClose, provider->desc.userdata,
                               reinterpret_cast<AbcUiDocument *>(this));
  }

public:
  module_resources::Registration Lifetime;

private:
  bool Closing = false;
  AbcUiDocumentDesc Desc;
  std::shared_ptr<RecoveryProvider> Recovery;
};

std::vector<std::shared_ptr<ModuleDocument>> &documents() {
  static std::vector<std::shared_ptr<ModuleDocument>> instance;
  return instance;
}

Rml::Context *&liveContext() {
  static Rml::Context *instance = nullptr;
  return instance;
}

ModuleDocument *resolve(AbcUiDocument *handle) {
  if (!handle)
    return nullptr;
  const auto *wanted = reinterpret_cast<ModuleDocument *>(handle);
  for (const auto &doc : documents()) {
    if (doc.get() == wanted)
      return doc.get();
  }
  return nullptr;
}

// One load attempt; the outcome is logged by InteractiveDocument itself
// (missing asset -> emergency page, Rml broken -> failed).
void loadDocument(ModuleDocument &doc) {
  if (Rml::Context *context = liveContext())
    doc.load(context);
}

} // namespace

AbcUiRecoveryRegistration registerRecovery(const AbcUiRecoveryDesc *desc) {
  const auto owner = module_resources::current();
  if (!owner || !owner->loading || !module_resources::accepting() ||
      recoveryProvider() || !desc ||
      desc->structSize < sizeof(AbcUiRecoveryDesc) || !desc->path)
    return 0;
  const std::string prefix = "mod://" + owner->id + "/";
  if (!std::string_view(desc->path).starts_with(prefix))
    return 0;
  static uint64_t next = 1;
  auto provider = std::make_shared<RecoveryProvider>();
  provider->id = next++;
  provider->desc = *desc;
  provider->path = desc->path;
  const uint64_t id = provider->id;
  provider->lifetime.track([id] { removeRecovery(id); });
  recoveryProvider() = std::move(provider);
  return id;
}

// Internal cleanup does not require a caller identity (failure rollback).
void removeRecovery(AbcUiRecoveryRegistration id) {
  const auto provider = recoveryProvider();
  if (!provider || provider->id != id)
    return;
  recoveryProvider().reset(); // prevent re-entrant loads using this provider
  const auto snapshot = documents();
  for (const auto &doc : snapshot)
    if (doc->usesRecovery(provider))
      doc->closeDocument();
}

void unregisterRecovery(AbcUiRecoveryRegistration id) {
  const auto provider = recoveryProvider();
  if (provider && provider->lifetime.owner() == module_resources::current())
    removeRecovery(id);
}

int32_t requestRetry(AbcUiDocument *handle) {
  const auto doc = resolve(handle);
  if (!doc || !doc->mayRetry() || !module_resources::accepting() ||
      !doc->isDegraded())
    return 0;
  doc->requestRetry();
  return 1;
}

AbcUiDocument *create(const AbcUiDocumentDesc &desc) {
  if (!module_resources::accepting())
    return nullptr;
  const auto owner = module_resources::current();
  if (!owner || !desc.path ||
      !std::string_view(desc.path).starts_with("mod://" + owner->id + "/"))
    return nullptr;
  if (!desc.name || !*desc.name) {
    abc::ConsoleWarn("[AbcUi] module document without a name refused\n");
    return nullptr;
  }
  if (!desc.path || !*desc.path) {
    abc::ConsoleWarn("[AbcUi] module document \"%s\" without a path refused\n",
                     desc.name);
    return nullptr;
  }
  for (const auto &existing : documents()) {
    if (existing->getName() == desc.name) {
      abc::ConsoleWarn("[AbcUi] module document \"%s\" registered twice, "
                       "refused\n",
                       desc.name);
      return nullptr;
    }
  }
  auto doc = std::make_shared<ModuleDocument>(desc);
  // The raw pointer stays valid across a re-entrant create from onReady: the
  // vector may reallocate, the heap object does not.
  ModuleDocument *raw = doc.get();
  documents().push_back(doc);
  auto *handle = reinterpret_cast<AbcUiDocument *>(raw);
  raw->Lifetime.track([handle] { destroy(handle); });
  loadDocument(*raw);
  return resolve(handle) ? handle : nullptr;
}

void destroy(AbcUiDocument *handle) {
  ModuleDocument *target = resolve(handle);
  if (!target)
    return;
  for (auto it = documents().begin(); it != documents().end(); ++it) {
    if (it->get() == target) {
      auto closing = *it;
      documents().erase(it); // forget before onClose can re-enter destroy
      closing->closeDocument();
      return;
    }
  }
}

bool isDocument(AbcUiDocument *doc) {
  return resolve(doc) != nullptr;
}

Rml::ElementDocument *documentOf(AbcUiDocument *doc) {
  ModuleDocument *document = resolve(doc);
  return document ? document->document() : nullptr;
}

int32_t visible(AbcUiDocument *doc) {
  ModuleDocument *document = resolve(doc);
  return document && document->isVisible() ? 1 : 0;
}

int32_t degraded(AbcUiDocument *doc) {
  ModuleDocument *document = resolve(doc);
  return document && document->isDegraded() ? 1 : 0;
}

void show(AbcUiDocument *doc) {
  if (ModuleDocument *document = resolve(doc))
    document->showDocument();
}

void hide(AbcUiDocument *doc) {
  if (ModuleDocument *document = resolve(doc))
    document->hideDocument();
}

int32_t retry(AbcUiDocument *doc) {
  ModuleDocument *document = resolve(doc);
  if (!document)
    return 0;
  const auto keepAlive = document->shared_from_this();
  if (Rml::Context *context = liveContext()) {
    const InteractiveDocument::LoadOutcome outcome = document->retry(context);
    if (outcome == InteractiveDocument::LoadOutcome::kFailed)
      return 0;
  }
  return document->isLoaded() ? 1 : 0;
}

void contextReady(Rml::Context *context) {
  liveContext() = context;
  module_resources::contextRegistrationsOpen() = context != nullptr;
  if (!context)
    return;
  const auto snapshot = documents();
  for (const auto &doc : snapshot)
    if (resolve(reinterpret_cast<AbcUiDocument *>(doc.get())))
      doc->load(context);
}

void pump() {
  Rml::Context *context = liveContext();
  if (!context)
    return;
  if (std::none_of(documents().begin(), documents().end(),
                   [](const auto &doc) { return doc->retryRequested(); }))
    return;
  const auto snapshot = documents();
  for (const auto &doc : snapshot) {
    if (!resolve(reinterpret_cast<AbcUiDocument *>(doc.get())) ||
        !doc->retryRequested())
      continue;
    doc->retry(context);
  }
}

void shutdown() {
  // onClose may register another document. Keep new descriptors unloaded
  // until the next context, rather than retaining a pointer into this one.
  liveContext() = nullptr;
  module_resources::contextRegistrationsOpen() = false;
  const auto snapshot = documents();
  for (const auto &doc : snapshot)
    doc->closeDocument();
}

void unload() {
  liveContext() = nullptr;
  module_resources::contextRegistrationsOpen() = false;
  const auto snapshot = documents();
  for (const auto &doc : snapshot)
    doc->closeDocument();
  documents().clear();
}

void hideMenu() {
  const auto snapshot = documents();
  for (const auto &doc : snapshot) {
    if (resolve(reinterpret_cast<AbcUiDocument *>(doc.get())) &&
        doc->menuLayer() && doc->isVisible())
      doc->hideDocument();
  }
}

bool ownsVisible(Rml::ElementDocument *document) {
  if (!document)
    return false;
  for (const auto &doc : documents()) {
    if (doc->document() == document && doc->isVisible())
      return true;
  }
  return false;
}

} // namespace module_documents
} // namespace framework
