#pragma once

// framework::HudPanel - panel base class. Plan 05 §4.2.
//
// A panel owns one Rml document plus the controller code that feeds it.
// Subclasses load their RML (linking Theme::kBaseSheetPath first), refresh
// DOM from data in onData, and toggle visibility through show/hide (which
// maps to the document's display property so hidden panels cost no layout).
// No data-binding library; controllers use SetInnerRML/SetAttribute
// directly (plan 05 §4.2).
//
// Event subscriptions: data-layer callbacks belong to the panel lifetime -
// register them in onDocumentReady and push their unsubscribe thunks through
// addCleanup(); the destructor runs cleanups before the document dies.

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include <functional>
#include <string>
#include <vector>

// abc/Events.h references xcommand_t (CChainedCommand) - the HLSDK typedef
// must land first (09 §2 include-order rule).
#include <command.h>
#include <abc/Events.h>

namespace framework {

class HudPanel {
public:
  // zIndex orders the panel's document against the other HUD documents
  // (RmlUi paints the context root's positioned children by z-index, then by
  // document order; 0 = default, negative = under every default document).
  HudPanel(const char *name, const char *documentPath, int zIndex = 0);
  virtual ~HudPanel();

  // Non-copyable: panels are registered once and owned by PanelManager.
  HudPanel(const HudPanel &) = delete;
  HudPanel &operator=(const HudPanel &) = delete;

  // Loads the document into the context; called by PanelManager::initAll.
  bool loadDocument(Rml::Context *context);

  void show();
  void hide();
  bool isVisible() const;
  // Session visibility is independent of a panel's data-driven request.
  // Menu documents do not inherit HudPanel and are unaffected.
  void setSessionActive(bool active);

  const std::string &getName() const {
    return Name;
  }
  Rml::ElementDocument *getDocument() const {
    return Document;
  }

  // Data->DOM refresh; PanelManager::onFrame forwards the pump.
  virtual void onData([[maybe_unused]] double frameTime) {}

  // Render-phase pump (HUD_Redraw, the old vgui think/paint phase): the
  // engine's view matrices are live here, so panels doing world->screen
  // projection reposition their elements in this phase and the framework
  // re-runs Context::Update afterwards (see PanelManager::onRedraw).
  virtual void onRedraw([[maybe_unused]] double frameTime) {}

  // Map transition (PanelManager::vidInitAll): back to the pre-map state.
  virtual void reset() {}

protected:
  // Post-load hook: query elements, subscribe data events.
  virtual void onDocumentReady() {}

  // Lifetime-ordered unsubscribe thunk (runs in the destructor).
  void addCleanup(std::function<void()> fn);

  // Subscribes a data-layer callback and arms its unsubscribe for the
  // panel lifetime (the standard onDocumentReady wiring).
  template <typename Sig, typename Fn>
  void subscribe(abc::CallbackList<Sig> &list, Fn &&callback) {
    const auto handle = list.append(std::forward<Fn>(callback));
    addCleanup([&list, handle]() { list.remove(handle); });
  }

  // Common element lookup with a warn-on-missing (panel layout typo guard).
  Rml::Element *element(const char *id);

  const std::string Name;
  const std::string DocumentPath;
  const int ZIndex;
  Rml::ElementDocument *Document = nullptr;

private:
  bool SessionActive = false;
  bool WantsVisible = false;
  std::vector<std::function<void()>> Cleanups;
};

} // namespace framework
