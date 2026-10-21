#pragma once

// panels::inventory - the RmlUi inventory window (issue 10).
//
// The page projects services::inventory::InventoryModel into the shared menu
// theme: title bar with a close button, the item list, the selected item's
// name and description, and the native window's three action buttons (Drop
// Item / Activate Item / Drop ALL Items). It replaces the client's VGUI
// CInventoryPanel; the chained inventory commands keep that window closed.
//
// Layout follows the native window's content (client.dll CInventoryPanel
// ctor + UpdateSelection): an empty list shows "You haven't got any
// inventory", a list without a selection shows "No item selected", an item
// without a name/description falls back to the native "Nameless item" /
// "No item description" strings, Activate/Drop are only available with a
// selection and Drop ALL only with items. The native window tints Activate/
// Drop red when the server flag forbids the action but still sends the
// command; the page mirrors that (warning class, still clickable).
//
// Localized button/status text and every server-derived string is escaped;
// the page never composes a command or touches the engine.

#include "../../framework/InteractiveDocument.h"
#include "../../services/inventory/InventoryModel.h"

#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Input.h>

#include <functional>
#include <cstdint>

namespace Rml {
class Element;
class Event;
} // namespace Rml

namespace panels::inventory {

class InventoryPage final : public framework::InteractiveDocument {
public:
  using ActionSink = std::function<void(services::inventory::Action)>;
  using CloseSink = std::function<void()>;

  InventoryPage();
  ~InventoryPage() override;

  void setModel(services::inventory::InventoryModel *model) {
    Model = model;
  }
  void setActionSink(ActionSink sink) {
    ActionSink_ = std::move(sink);
  }
  void setCloseSink(CloseSink sink) {
    CloseSink_ = std::move(sink);
  }

  // Fresh opening: re-project from the model and focus the current row.
  void prepare();
  // Re-projects only when the model revision changed.
  void render();
  void refocusDefault();

  // Keyboard belongs to the game-menu input owner; the page maps its keys to
  // list/selection intents. Esc escapes to the close sink (exactly once).
  bool handleKey(Rml::Input::KeyIdentifier key);

protected:
  void onDocumentReady() override;

private:
  class PageListener final : public Rml::EventListener {
  public:
    explicit PageListener(InventoryPage *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    InventoryPage *Owner;
  };

  void handleClick(Rml::Event &event);
  void runAction(services::inventory::Action action);
  void selectRow(std::uint64_t serial);
  void setButtonState(const char *id, bool enabled, bool warning);

  ActionSink ActionSink_;
  CloseSink CloseSink_;
  services::inventory::InventoryModel *Model = nullptr;
  PageListener Listener;
  std::uint64_t RenderedRevision = static_cast<std::uint64_t>(-1);
};

} // namespace panels::inventory
