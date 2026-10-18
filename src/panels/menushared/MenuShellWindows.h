#pragma once

// panels::menushared - the two small shared menu windows of the T09 shell
// (03 doc explicitly allows a shared confirm component):
//
//   ConfirmQuitOverlay - the quit confirmation modal (the original QueryBox
//   parity: confirming is the only path that reaches the backend, cancelling
//   never quits). The safe default focus sits on Cancel, so a stray Enter
//   cannot quit the game. Like every menu window it only reports intents;
//   the sync owns its visibility.
//
//   MenuStubPage - the "not yet implemented" surface every unfinished menu
//   feature routes into (T09 step 2): the parity TODO is user-visible, the
//   Return entry goes back through the controller, and no original window is
//   ever opened. Two instances exist in the module: the sub-page stub
//   (settings/browser/create/playerlist/loading placeholders) and the
//   console placeholder overlay (the real console is T16).

#include "../../framework/InteractiveDocument.h"

#include <RmlUi/Core/EventListener.h>

#include <functional>
#include <string>

namespace Rml {
class Event;
}

namespace panels::menushared {

// Same seam contract as panels::mainmenu::ActionSink (fixed page-internal
// action names, never user strings).
using ActionSink = std::function<void(const char *action)>;

class ConfirmQuitOverlay final : public framework::InteractiveDocument {
public:
  ConfirmQuitOverlay();
  ~ConfirmQuitOverlay() override;

  void setActionSink(ActionSink sink) {
    Sink = std::move(sink);
  }

  // Re-takes the safe default (cancel) when a focus thief (debug hook,
  // hidden-document hand-off) took the context focus; the module's frame
  // guard calls this.
  void refocusDefault();

protected:
  void onDocumentReady() override;

private:
  class WindowListener final : public Rml::EventListener {
  public:
    explicit WindowListener(ConfirmQuitOverlay *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    ConfirmQuitOverlay *Owner;
  };

  void handle(Rml::Event &event);

  WindowListener Listener;
  ActionSink Sink;
};

class MenuStubPage final : public framework::InteractiveDocument {
public:
  // `name` distinguishes the two instances ("menusub" page stub,
  // "menuconsole" overlay placeholder) - the document id rides the rml.
  MenuStubPage(const char *name, const char *documentPath);
  ~MenuStubPage() override;

  void setActionSink(ActionSink sink) {
    Sink = std::move(sink);
  }

  // Points the stub at one unfinished surface. `title`/`body` are display
  // strings already resolved by the caller ("#token" through Localization
  // or plain text); they are copied and escaped before they reach the DOM.
  void showFor(const char *title, const char *body);

  // Re-takes the return-entry focus (module's per-frame focus guard).
  void refocusDefault();

protected:
  void onDocumentReady() override;

private:
  class WindowListener final : public Rml::EventListener {
  public:
    explicit WindowListener(MenuStubPage *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    MenuStubPage *Owner;
  };

  void handle(Rml::Event &event);

  WindowListener Listener;
  ActionSink Sink;
};

} // namespace panels::menushared
