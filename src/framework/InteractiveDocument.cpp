// framework::InteractiveDocument implementation - see InteractiveDocument.h.
#include "InteractiveDocument.h"
#include "MenuBackdrop.h"

#include "DocumentLoadGuard.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/EventListener.h>

#include <abc/Console.h>

#include <cstdio>
#include <utility>

namespace framework {

namespace {

// Emergency page (T05 step 5): inline RML so it never depends on the
// external menu assets it exists to replace. Deliberately plain: no
// localized #tokens (the lang table is an external asset too), no image
// sources, dp units only - the context density ratio (Theme::applyScale)
// scales it like every other document, no second sizing logic. The %s is the
// owner's document name so context->GetDocument() keeps resolving it. Char
// array, not pointer: sizeof below is the load buffer's sizing contract.
//
// T21: the page carries the two fault-policy affordances - Retry (re-attempt
// the main document) and Quit (controlled exit). Fixed English labels on
// purpose: the localization assets may be exactly what failed. Both buttons
// are inert until their listeners attach below (the quit click reports
// through the injected sink; no sink = no action, never a crash).
//
// The inline stylesheet sets its own font-family: the page deliberately does
// NOT load base.rcss (that is an external asset like any other), and without
// an explicit family RmlUi resolves text to nothing - live-caught as a
// glyph-less emergency page during the T21a fault matrix. Verdana is the
// first face of the loader's list (rmlui::loadFonts); a missing face falls
// back through RmlUi's registered fallback chain.
constexpr char kEmergencyRml[] =
    "<rml>"
    "<head>"
    "<style>"
    "	body { background-color: #000000; font-family: Verdana; }"
    "	div { display: block; }"
    "	#panel { position: absolute; left: 60dp; top: 60dp;"
    "		width: 400dp; background-color: #101418; padding: 20dp;"
    "		border: 1dp #3a4148; }"
    "	h1 { display: block; font-size: 14dp; font-weight: bold;"
    "		color: #ff5500; }"
    "	p { display: block; font-size: 10dp; color: #dedede; }"
    "	button { display: inline-block; margin-right: 8dp; padding: 4dp 10dp;"
    "		font-size: 10dp; color: #dedede; background-color: #23292f;"
    "		border: 1dp #3a4148; }"
    "</style>"
    "</head>"
    "<body id=\"%s\">"
    "<div id=\"panel\">"
    "<h1>AbcUi emergency mode</h1>"
    "<p>Menu resources are missing or broken.</p>"
    "<p>Check the console log; the UI stays out of the way until they are "
    "fixed.</p>"
    "<button id=\"emergency-retry\">Retry</button>"
    "<button id=\"emergency-quit\">Quit</button>"
    "</div>"
    "</body>"
    "</rml>";

} // namespace

InteractiveDocument::InteractiveDocument(const char *name,
                                         const char *documentPath,
                                         bool withBackdrop)
    : Name(name), DocumentPath(documentPath), WithBackdrop(withBackdrop) {}

InteractiveDocument::~InteractiveDocument() {
  close();
}

void InteractiveDocument::EmergencyButtonListener::ProcessEvent(Rml::Event &) {
  if (IsQuit) {
    // The exit intent reaches the owner's sink; without one the button
    // stays inert (the console `quit` command and the OS close remain).
    if (Owner->EmergencyHandler)
      Owner->EmergencyHandler(kEmergencyQuitIntent);
  } else {
    // Marked only - the owner retries on its own beat so this document
    // is never closed from inside its own dispatch (see the header).
    Owner->RetryRequested = true;
  }
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
  char rml[sizeof(kEmergencyRml) + 64];
  std::snprintf(rml, sizeof(rml), kEmergencyRml, Name.c_str());
  Document = context->LoadDocumentFromMemory(rml);
  if (!Document)
    return LoadOutcome::kFailed;
  Degraded = true;
  RetryRequested = false;
  // The recovery affordances: bound through the owner-tracked attach so
  // close() detaches them before the DOM dies.
  if (Rml::Element *retry = Document->GetElementById("emergency-retry"))
    addListener(retry, "click", &RetryButtonListener);
  if (Rml::Element *quit = Document->GetElementById("emergency-quit"))
    addListener(quit, "click", &QuitButtonListener);
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
  Degraded = false;
  RetryRequested = false;
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
