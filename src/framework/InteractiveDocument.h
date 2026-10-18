#pragma once

// framework::InteractiveDocument - owner of one interactive (non-HUD) Rml
// document on the shared abcui context (T05).
//
// The passive HUD panels load through HudPanel: pointer-events none,
// FocusFlag::None - they must never intercept hit testing or steal the
// context focus. Interactive documents are the complement: shown with
// document focus (autofocus rides Rml's FocusFlag::Auto), pulled to the
// front of the context z-order, hit-testable, and closable.
//
// Listener lifetime is the owner's job: every listener attached through
// addListener() is recorded and detached again in close() BEFORE the
// document is closed, so no handler can ever fire on (or after) DOM
// teardown. The same contract keeps borrowed element pointers safe: after
// close() the document pointer is null and nothing reachable references the
// dead DOM.
//
// Render path: documents render through the existing single-context frame
// path (rmlui::onFrame/onRedraw -> Context::Update, OnRenderEndFrame ->
// Context::Render). This class adds no context, renderer or second render
// pass (T05 forbidden list).

#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/ObserverPtr.h>

#include <functional>
#include <string>
#include <vector>

namespace Rml {
class Context;
class Element;
} // namespace Rml

namespace framework {

class InteractiveDocument {
public:
  // Explicit outcome of load() (T05 step 5): a missing main document is a
  // clear result, never a silent success. The full strict-failure policy is
  // T21; this only carries the emergency-document entry.
  enum class LoadOutcome {
    kLoaded,            // main document loaded from its file path
    kEmergencyFallback, // main document missing/broken; built-in emergency
                        // page loaded instead (inline RML, no external
                        // menu assets)
    kFailed,            // even the emergency page failed (Rml core broken);
                        // caller escalates, nothing is loaded
  };

  // `withBackdrop`: the page is a full-window menu surface and wants the
  // shared blurred backdrop inserted behind its own content (see
  // framework/MenuBackdrop.h; a no-op until the rmlui layer installs the
  // implementation). Overlays that must keep the world visible (the console)
  // leave it false.
  InteractiveDocument(const char *name, const char *documentPath,
                      bool withBackdrop = false);
  // Subclasses with member listeners MUST call close() in their destructor
  // body, before those members die. The base destructor is only a fallback.
  virtual ~InteractiveDocument();

  InteractiveDocument(const InteractiveDocument &) = delete;
  InteractiveDocument &operator=(const InteractiveDocument &) = delete;

  // Loads the main document; on failure loads the built-in emergency page
  // instead (degraded mode, isDegraded()). Safe to call again after
  // close(); calling on an already-loaded document is a no-op returning the
  // outcome of the earlier load.
  LoadOutcome load(Rml::Context *context);

  // Show: visible, context focus (autofocus element wins), front of the
  // context z-order. Idempotent while visible: a repeated show neither
  // re-focuses nor re-orders (HudPanel::show's steal-the-focus guard, the
  // interactive counterpart).
  void show();
  // Hide: visibility only. Listener bindings survive hide/show cycles.
  // Context-focus release when no other document exists is RmlUi's own
  // behavior (UnfocusDocument keeps the pointer with nothing to hand to);
  // real focus-release semantics are T06's input ownership.
  void hide();
  // Close: detach every listener, then close the document. After close()
  // the object is back to the pre-load state and may load() again.
  void close();

  bool isLoaded() const {
    return Document != nullptr;
  }
  // True when the loaded document is the emergency fallback, not the main
  // document (callers surface this instead of pretending normal service).
  bool isDegraded() const {
    return Document != nullptr && Degraded;
  }
  bool isVisible() const;
  Rml::ElementDocument *document() const {
    return Document;
  }
  const std::string &getName() const {
    return Name;
  }

  // --- T21 emergency affordances -------------------------------------
  // The built-in emergency page is the "minimal Rml emergency page" of the
  // fault policy (03 doc): resources failed, but Rml itself works, so the
  // page must offer a recovery resource and an exit. Its two buttons carry
  // fixed intents and NO localization (the lang table is one of the
  // external assets that may be exactly what failed).
  //
  // Retry is owned by this class: the click only marks the request
  // (retryRequested); the owner performs retry() on its own beat so the
  // document is never closed from inside its own event dispatch.
  // Quit needs the owner's engine reach and goes through the injected
  // sink as kEmergencyQuitIntent; a null sink leaves the button inert
  // (every other surface - console `quit`, OS close - still exits).
  static constexpr const char *kEmergencyQuitIntent = "emergency-quit";

  void setEmergencySink(std::function<void(const char *intent)> sink) {
    EmergencyHandler = std::move(sink);
  }
  // A retry click happened on the emergency page (see above). Cleared by
  // retry() and close().
  bool retryRequested() const {
    return RetryRequested;
  }
  // Re-attempt the main document on `context` (close + load): the
  // production recovery path for a degraded document. Safe while hidden;
  // an already-healthy document is left untouched.
  LoadOutcome retry(Rml::Context *context);

protected:
  // Post-load hook for the real document: resolve elements and attach
  // listeners through addListener(). Never fires for the emergency page
  // (its DOM has none of the page's elements; degrade state is queryable).
  virtual void onDocumentReady() {}

  // Owner-tracked listener attach; detached again before Close() runs.
  void addListener(Rml::Element *target, const char *event,
                   Rml::EventListener *listener, bool capturePhase = false);

  // Common lookup with a warn-on-missing guard (HudPanel parity).
  Rml::Element *element(const char *id);

private:
  struct ListenerBinding {
    Rml::ObserverPtr<Rml::Element> Target;
    std::string Event;
    Rml::EventListener *Listener;
    bool Capture;
  };

  // Emergency-page buttons (attached in the emergency branch of load()):
  // the retry one marks RetryRequested, the quit one reports through the
  // injected sink. Both are member objects so they outlive the emergency
  // document; close() detaches them from the DOM first.
  class EmergencyButtonListener final : public Rml::EventListener {
  public:
    EmergencyButtonListener(InteractiveDocument *owner, bool isQuit)
        : Owner(owner), IsQuit(isQuit) {}
    void ProcessEvent(Rml::Event &) override;

  private:
    InteractiveDocument *Owner;
    bool IsQuit;
  };

  const std::string Name;
  const std::string DocumentPath;
  const bool WithBackdrop;
  Rml::ElementDocument *Document = nullptr;
  bool Degraded = false;
  bool RetryRequested = false;
  std::function<void(const char *intent)> EmergencyHandler;
  EmergencyButtonListener RetryButtonListener{this, false};
  EmergencyButtonListener QuitButtonListener{this, true};
  std::vector<ListenerBinding> Listeners;
};

} // namespace framework
