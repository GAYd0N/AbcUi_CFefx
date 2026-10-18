#pragma once

// panels::commandmenu - the RmlUi in-game command menu.
//
// The page projects services::commandmenu::Model into the shared menu theme
// as a translucent, left-anchored stack of at most three columns. It has no
// full-window backdrop: the world remains visible behind it. Only the active
// column's selection is interactive; fixed command/close intents cross the
// page boundary, while navigation stays page/model-local.

#include "../../framework/InteractiveDocument.h"
#include "../../services/commandmenu/CommandMenuModel.h"

#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Input.h>

#include <functional>
#include <string>

namespace Rml {
class Element;
class Event;
} // namespace Rml

namespace panels::commandmenu {

class CommandMenuPage final : public framework::InteractiveDocument {
public:
  using ActionSink = std::function<void(const char *action)>;
  using CommandSink = std::function<void(const std::string &command)>;
  using CloseSink = std::function<void()>;

  CommandMenuPage();
  ~CommandMenuPage() override;

  void setModel(services::commandmenu::Model *model) {
    Model = model;
  }
  // Kept for the shared game-menu wiring shape; command-menu actions are
  // resolved inside this page and never escape as generic navigation rows.
  void setActionSink(ActionSink sink) {
    ActionSink_ = std::move(sink);
  }
  void setCommandSink(CommandSink sink) {
    CommandSink_ = std::move(sink);
  }
  void setCloseSink(CloseSink sink) {
    CloseSink_ = std::move(sink);
  }

  // Fresh opening for the live map: resets the model to the root and
  // re-projects the columns.
  void prepare(const std::string &mapName);
  // Re-projects only when the model revision changed.
  void render();
  void refocusDefault();

  // Keyboard is owned by the module's input owner; this keeps the page's
  // navigation policy testable without engine headers.
  bool handleKey(Rml::Input::KeyIdentifier key);

  static constexpr std::size_t kVisibleColumns = 3;

protected:
  void onDocumentReady() override;

private:
  class PageListener final : public Rml::EventListener {
  public:
    explicit PageListener(CommandMenuPage *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    CommandMenuPage *Owner;
  };

  void handleClick(Rml::Event &event);
  bool finish(const services::commandmenu::ActionResult &result);
  void resolveLabels();
  Rml::Element *node(const char *id) const;
  void focusSelected();

  CommandSink CommandSink_;
  CloseSink CloseSink_;
  ActionSink ActionSink_;
  services::commandmenu::Model *Model = nullptr;
  PageListener Listener;
  std::size_t RenderedRevision = static_cast<std::size_t>(-1);
};

} // namespace panels::commandmenu
