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
    kEmergencyFallback, // main document missing/broken; provider page loaded
    kFailed,            // no recovery provider or its document also failed;
                        // caller escalates, nothing is loaded
  };

  // Cross-document z-order tiers. Every menu surface is one document on the
  // shared context, and RmlUi sorts the context root's stacking context by
  // z-index first: any explicit positive z-index paints (and hit-tests)
  // above every default document, whose auto z-index leaves it in tree
  // order. Only a layer that must outrank a peer needs a tier - the same
  // mechanism HudPanel uses for the HUD plates (-1 radar):
  //   default documents (page shells, console)
  //     < kLoadingZIndex
  //     < kConnectionDialogZIndex (connection error / password / direct
  //       connect)
  //     < kConfirmQuitZIndex.
  // Loading covers the page and console layers (issue #4); the three
  // connection dialogs stay above it because they share loading's dialog
  // slot (loading.rcss gives every one of the four boxes the same geometry)
  // and the password/direct-connect ones are only actionable through their
  // own buttons. The property is set once at load and never toggled; a
  // hidden document leaves the stacking context entirely, so a hidden
  // loading page leaves no z-order residue.
  static constexpr int kLoadingZIndex = 500;
  static constexpr int kConnectionDialogZIndex = 550;
  static constexpr int kConfirmQuitZIndex = 600;

  // `withBackdrop`: the page is a full-window menu surface and wants the
  // shared blurred backdrop inserted behind its own content (see
  // framework/MenuBackdrop.h; a no-op until the rmlui layer installs the
  // implementation). Overlays that must keep the world visible (the console)
  // leave it false.
  // `zIndex`: the document's tier in the cross-document order above (0 =
  // default, ordered with every other default document by show order).
  InteractiveDocument(const char *name, const char *documentPath,
                      bool withBackdrop = false, int zIndex = 0);
  // Subclasses with member listeners MUST call close() in their destructor
  // body, before those members die. The base destructor is only a fallback.
  virtual ~InteractiveDocument();

  InteractiveDocument(const InteractiveDocument &) = delete;
  InteractiveDocument &operator=(const InteractiveDocument &) = delete;

  // Loads the main document; on failure loads the registered recovery page
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

  // Retry requests are consumed by the frame pump, never DOM dispatch.
  void requestRetry() {
    RetryRequested = true;
  }
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
  virtual const char *recoveryPath() {
    return nullptr;
  }
  virtual void onRecoveryReady() {}
  virtual void onRecoveryClose() {}

  // Owner-tracked listener attach; detached again before Close() runs.
  void addListener(Rml::Element *target, const char *event,
                   Rml::EventListener *listener, bool capturePhase = false);

  // Common lookup with a warn-on-missing guard (HudPanel parity).
  Rml::Element *element(const char *id);

private:
  // Applies the constructor's z-index to whichever document load() ended up
  // with (main or emergency); no-op for a default document.
  void applyZIndex();

  struct ListenerBinding {
    Rml::ObserverPtr<Rml::Element> Target;
    std::string Event;
    Rml::EventListener *Listener;
    bool Capture;
  };

  const std::string Name;
  const std::string DocumentPath;
  const bool WithBackdrop;
  const int ZIndex;
  Rml::ElementDocument *Document = nullptr;
  bool Degraded = false;
  bool RetryRequested = false;
  std::vector<ListenerBinding> Listeners;
};

} // namespace framework
