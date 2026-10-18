#pragma once

// panels::votemenu - the game-menu vote creation page.
//
// Replaces ABCEnchance's VGUI CVoteMenuDialog on the AbcUi menu stack. The
// page projects services::votemenu::VoteMenuModel into the shared menu theme:
// four tabs, a filtered list, one selected entry and the primary/cancel
// actions. Rows and tabs are page-local choices; only Submit/Cancel cross the
// page boundary as fixed intents. All player/map-derived text is escaped.

#include "../../framework/InteractiveDocument.h"
#include "../../services/votemenu/VoteMenuModel.h"

#include <RmlUi/Core/EventListener.h>

#include <functional>
#include <string>

namespace Rml {
class Element;
class Event;
} // namespace Rml

namespace panels::votemenu {

using ActionSink = std::function<void(const char *action)>;

class VoteMenuPage final : public framework::InteractiveDocument {
public:
  VoteMenuPage();
  ~VoteMenuPage() override;

  void setActionSink(ActionSink sink) {
    Sink = std::move(sink);
  }
  void setModel(services::votemenu::VoteMenuModel *model) {
    Model = model;
  }

  // Fresh opening: model resets to Ban, filter/selection clear, live roster.
  void prepare();
  // Re-projects the model (change-gated by its revision).
  void render();
  // Inline feedback: a "#token" or plain text, escaped before the DOM.
  void showHint(const char *text);
  void refocusDefault();

protected:
  void onDocumentReady() override;

private:
  class PageListener final : public Rml::EventListener {
  public:
    explicit PageListener(VoteMenuPage *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    VoteMenuPage *Owner;
  };

  void handleClick(Rml::Event &event);
  void setFilterText(const char *text);
  Rml::Element *node(const char *id) const;

  ActionSink Sink;
  services::votemenu::VoteMenuModel *Model = nullptr;
  PageListener Listener;
  std::size_t RenderedRevision = static_cast<std::size_t>(-1);
};

} // namespace panels::votemenu
