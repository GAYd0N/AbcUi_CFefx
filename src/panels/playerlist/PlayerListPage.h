#pragma once

// panels::playerlist - the T20 player-list page (one interactive document at
// the kPlayerList menu page, replacing the T09 stub; the shipped surface is
// Resource/PlayerListDialog.res - a Frame with a ListPanel plus a Mute button
// and Close).
//
// The page is a PROJECTION of services::playerlist::PlayerListModel (the
// identity/selection/mute model): it reads the model's roster snapshot and
// reports fixed action intents (select / mute-toggle / back). It never writes
// an engine value and never invents a mute backend - the model owns the
// read-back discipline and reports "unavailable" honestly when the shipped
// interface is absent (see MuteBackend.h).
//
// Parity points taken from the shipped dialog (Ghidra evidence in
// PlayerListModel.h / MuteBackend.h):
//   - rows carry name + "Properties" (the shipped column pair): the page
//     shows the name plus a state suffix (Muted / CPU Player / Spectating),
//     which is where the shipped refresh put those very strings.
//   - the Mute button label toggles #MuteIngameVoice / #UnmuteIngameVoice and
//     is DISABLED when the backend is missing or no mutable player is
//     selected - the exact enable rule of FUN_10015e30.
//   - Close == the shipped Button1 (Command "Close"): a plain back intent.
//
// The page escapes every player-derived string (hostile names render as
// text) and keeps the selected row across refreshes by the model's identity
// key, so a leaving player clears the selection instead of retargeting it.

#include "../../framework/InteractiveDocument.h"
#include "../../services/playerlist/PlayerListModel.h"

#include <RmlUi/Core/EventListener.h>

#include <functional>
#include <string>

namespace Rml {
class Element;
class Event;
} // namespace Rml

namespace panels {
namespace playerlist {

using ActionSink = std::function<void(const char *action)>;

class PlayerListPage final : public framework::InteractiveDocument {
public:
  PlayerListPage();
  ~PlayerListPage() override;

  void setActionSink(ActionSink sink) {
    Sink = std::move(sink);
  }
  using SelectionSink =
      std::function<void(const services::playerlist::SelectionKey &)>;
  void setSelectionSink(SelectionSink Handler) {
    SelectionHandler = std::move(Handler);
  }
  void setModel(services::playerlist::PlayerListModel *model) {
    Model = model;
  }

  // Re-projects the model snapshot into the DOM (rows, selection highlight,
  // mute label/enable, status line). Called on presentation and on every
  // model change (the module's frame beat).
  void render();

  // Re-takes the safe default focus (module's per-frame focus guard): the
  // first row when one exists, else the close button.
  void refocusDefault();

protected:
  void onDocumentReady() override;

private:
  class PageListener final : public Rml::EventListener {
  public:
    explicit PageListener(PlayerListPage *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    PlayerListPage *Owner;
  };

  void handleClick(Rml::Event &event);
  Rml::Element *node(const char *id) const;

  ActionSink Sink;
  SelectionSink SelectionHandler;
  PlayerListPage *Self = nullptr;
  services::playerlist::PlayerListModel *Model = nullptr;
  PageListener Listener;
  // Revision of the last projection (skip redundant rebuilds).
  std::size_t RenderedRevision = static_cast<std::size_t>(-1);
};

} // namespace playerlist
} // namespace panels
