// framework::InteractiveDocument implementation - see InteractiveDocument.h.
#include "InteractiveDocument.h"
#include "MenuBackdrop.h"

#include "DocumentLoadGuard.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/EventListener.h>

#include <abc/Console.h>

#include <utility>

namespace framework {

InteractiveDocument::InteractiveDocument(const char *name,
                                         const char *documentPath,
                                         bool withBackdrop, int zIndex)
    : Name(name), DocumentPath(documentPath), WithBackdrop(withBackdrop),
      ZIndex(zIndex) {}

InteractiveDocument::~InteractiveDocument() {
  close();
}

InteractiveDocument::LoadOutcome
InteractiveDocument::load(Rml::Context *context) {
  if (!context)
    return LoadOutcome::kFailed;
  if (Document)
    return Degraded ? LoadOutcome::kEmergencyFallback : LoadOutcome::kLoaded;

  // T21: bracket the main-document load (see DocumentLoadGuard.h). RmlUi's
  // parser is lenient - a syntax-broken file yields a PARTIALLY parsed,
  // non-null document plus "XML parse error" warnings; appending that DOM
  // would present an empty black page (live-caught in the fault matrix).
  // A failure signal inside the load window closes the partial document
  // and redirects into the emergency path below.
  const uint64_t loadMark = documentload::begin();
  Document = context->LoadDocument(DocumentPath);
  if (Document && documentload::failedSince(loadMark)) {
    abc::ConsoleWarn("[AbcUi] interactive document \"%s\": %s failed to parse "
                     "cleanly, discarding partial document\n",
                     Name.c_str(), DocumentPath.c_str());
    Document->Close();
    Document = nullptr;
  }
  if (Document) {
    Degraded = false;
    applyZIndex();
    // Backdrop before the page's own setup: the page's onDocumentReady may
    // resolve or reorder nodes, and the backdrop must already sit behind
    // them (paint order is DOM order).
    if (WithBackdrop)
      menubackdrop::attach(Document);
    onDocumentReady();
    return LoadOutcome::kLoaded;
  }

  // The missing/broken main document is a clear, logged outcome - never silent.
  abc::ConsoleWarn("[AbcUi] interactive document \"%s\": failed to load %s, "
                   "falling back to emergency page\n",
                   Name.c_str(), DocumentPath.c_str());
  const char *path = recoveryPath();
  if (!path || !*path)
    return LoadOutcome::kFailed;
  const uint64_t recoveryMark = documentload::begin();
  Document = context->LoadDocument(path);
  if (Document && documentload::failedSince(recoveryMark)) {
    Document->Close();
    Document = nullptr;
  }
  if (!Document)
    return LoadOutcome::kFailed;
  Degraded = true;
  RetryRequested = false;
  applyZIndex();
  onRecoveryReady();
  return LoadOutcome::kEmergencyFallback;
}

InteractiveDocument::LoadOutcome
InteractiveDocument::retry(Rml::Context *context) {
  if (Document && !Degraded)
    return LoadOutcome::kLoaded; // healthy: nothing to recover
  close();
  return load(context);
}

void InteractiveDocument::show() {
  if (!Document || Document->IsVisible())
    return;
  // FocusFlag::Auto + the document's autofocus attribute: first show lands
  // on the page's primary element, re-shows land the same way. Modal stays
  // off - input ownership arbitration is T06, not a document property.
  Document->Show(Rml::ModalFlag::None, Rml::FocusFlag::Auto);
  // Show() does not order the context's document stack; interactive
  // windows own their z-order explicitly.
  Document->PullToFront();
}

void InteractiveDocument::hide() {
  // Idempotent like show(): Rml's Hide re-dispatches the hide event and
  // unfocuses the document every call, which would churn focus each time a
  // sync re-hides an already hidden document.
  if (Document && Document->IsVisible())
    Document->Hide();
}

void InteractiveDocument::close() {
  if (!Document)
    return;
  // Invalidate ownership before callbacks: OnDetach/Close can re-enter.
  Rml::ElementDocument *ClosingDocument = Document;
  Document = nullptr;
  const bool WasDegraded = Degraded;
  Degraded = false;
  RetryRequested = false;
  if (WasDegraded)
    onRecoveryClose();
  auto Bindings = std::move(Listeners);
  Listeners.clear();
  for (const ListenerBinding &Binding : Bindings) {
    if (Rml::Element *Target = Binding.Target.get())
      Target->RemoveEventListener(Binding.Event, Binding.Listener,
                                  Binding.Capture);
  }
  ClosingDocument->Close();
}

bool InteractiveDocument::isVisible() const {
  return Document && Document->IsVisible();
}

void InteractiveDocument::applyZIndex() {
  // Cross-document order only (see the tier constants in the header): RmlUi
  // paints and hit-tests the context root's stacking context by z-index
  // first, so a positive tier covers every default document. Set once at
  // load, never toggled - a hidden document is not in the stacking context,
  // so the visible order returns on its own when the layer hides.
  if (Document && ZIndex != 0)
    Document->SetProperty("z-index", std::to_string(ZIndex));
}

void InteractiveDocument::addListener(Rml::Element *target, const char *event,
                                      Rml::EventListener *listener,
                                      bool capturePhase) {
  if (!target || !listener)
    return;
  target->AddEventListener(event, listener, capturePhase);
  Listeners.push_back(
      {target->GetObserverPtr(), event, listener, capturePhase});
}

Rml::Element *InteractiveDocument::element(const char *id) {
  Rml::Element *found = Document ? Document->GetElementById(id) : nullptr;
  if (!found)
    abc::ConsoleWarn(
        "[AbcUi] interactive document \"%s\": element \"%s\" missing\n",
        Name.c_str(), id);
  return found;
}

} // namespace framework
