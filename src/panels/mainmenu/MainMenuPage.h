#pragma once

// panels::mainmenu - the T09 menu shell. ONE interactive document carrying
// the two home sections of the state machine: kMain (no-map) and kPause
// (in-game). Which section is visible is presentation of the controller's
// page stack - the page itself never infers game state from the DOM, it is
// told (showSection / setEntryVisible) by the gameui module on every sync.
//
// Every entry carries a fixed data-action name; clicks are delegated through
// one bubbling document listener and handed to the injected ActionSink.
// Actions are intents only: nothing here hides the document or runs engine
// work - the module translates the action into a controller event and the
// sync owns every visibility change. The original GameUI windows are never
// opened (T05 forbidden list stands).
//
// Focus: tab-index rides the stylesheet (auto per entry); Enter/Space
// activation is RmlUi's focused-element click; the visible focus state is
// the data-label status line. Default focus per section is the section's
// first visible entry (setEntryVisible first, then showSection).
//
// The document path defaults to the shipped shell; the offline text/IME
// tests load their T05 fixture through the same class (same wiring, same
// delegation, same focus mechanics).

#include "../../framework/InteractiveDocument.h"
#include "services/uiconfig/UiDefinitions.h"
#include "services/friends/FriendsModel.h"

#include <RmlUi/Core/EventListener.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace Rml {
class Event;
}

namespace panels::mainmenu {

// Injected business-action seam; invoked on the main thread from click
// handlers. Empty sink = actions are dropped (the page stays functional).
using ActionSink = std::function<void(const char *action)>;
// Join intent: one friend row's address was chosen for a connection. The page
// never validates or issues commands; the module owns the engine edge.
using FriendJoinSink =
    std::function<void(uint64_t steamId, const char *address)>;

class MainMenuPage final : public framework::InteractiveDocument {
public:
  enum class Section {
    kMain,  // no-map home (servers/creategame/settings/console/quit)
    kPause, // in-game home (resume/disconnect/playerlist/settings/quit)
  };

  explicit MainMenuPage(
      const char *documentPath = "abc/abcui/gameui/rml/mainmenu.rml");
  ~MainMenuPage() override;

  void setActionSink(ActionSink sink) {
    Sink = std::move(sink);
  }
  void setFriendJoinSink(FriendJoinSink sink) {
    JoinSink = std::move(sink);
  }
  void setDefinition(const services::uiconfig::MenuDefinition &Value) {
    Definition = Value;
  }
  void setMultiplayer(bool Value);

  // Character card facts (WS4): the player name line and the Studio model the
  // live <model> element renders. Both come from the module (engine facts),
  // never from the DOM; an empty model path hides the preview box.
  void setPlayerCard(const char *playerName, const char *modelPath,
                     const char *modelLabel);
  // Footer right-hand note (engine build). Empty clears it.
  void setFooterNote(const char *text);

  // Friends board projection (WS4): one row per entry, the local player
  // first; the row's avatar resolves through avatar://<steamid> and the
  // state line shows the game - a friend in a game that is not this one is
  // reported as such, never by AppID. Re-projection is the caller's
  // decision (the model's generation), so a steady list writes no DOM.
  void setFriends(const std::vector<services::friends::FriendEntry> &rows,
                  bool steamAvailable);

#ifndef NDEBUG
  // Debug seams (abc_ui_friends_test): exercise the row index and the join
  // intent without an injectable mouse.
  void debugOpenFriendMenu(Rml::Element *row, int x, int y);
  void debugJoinFriend(const char *steamId);
#endif

  // Makes one section the visible one and, when requested, lands keyboard
  // focus on the section's first visible entry. Safe while hidden: the
  // focus lands after the next show.
  void showSection(Section section, bool focusDefault);

  // Entry availability fed from backend facts (maxplayers singleplayer
  // check), never inferred from the DOM. `action` is one of the conditional
  // entries' fixed names (servers / creategame / playerlist); always-on
  // entries ignore this.
  void setEntryVisible(const char *action, bool visible);

protected:
  void onDocumentReady() override;

private:
  // The page's single listener instance: clicks attach to the document
  // (bubbling), focus/blur attach per data-label element (non-bubbling
  // per RmlUi's event spec). The owner detaches every binding before the
  // document closes (InteractiveDocument contract).
  class PageListener final : public Rml::EventListener {
  public:
    explicit PageListener(MainMenuPage *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    MainMenuPage *Owner;
  };

  void handle(Rml::Event &event);
  bool handleContextMenu(Rml::Event &event);
  void openFriendMenu(const char *steamId, const char *address, int x, int y);
  void closeFriendMenu();
  void buildEntries();
  void updateFocusStatus(Rml::Element *focused);
  void attachFocusBindings(Rml::Element *node);
  void setEntryVisibleRecursive(Rml::Element *node, const char *action,
                                bool visible);
  Rml::Element *sectionNode(Section section);

  PageListener Listener;
  ActionSink Sink;
  FriendJoinSink JoinSink;
  // The friend the open context menu belongs to (empty = closed).
  std::string MenuFriendId;
  std::string MenuFriendAddress;
  uint32_t FriendsGeneration = 0;
  services::uiconfig::MenuDefinition Definition =
      services::uiconfig::DefinitionParser::fallbackMenu();
  bool Multiplayer = false;
  Section VisibleSection = Section::kMain;
  // Entry availability facts fed by the module (maxplayers): the focus
  // scan reads these instead of the DOM - an element property only takes
  // effect at the next layout pass, while the sync needs the answer now.
  std::map<std::string, bool> EntryVisible;
};

} // namespace panels::mainmenu
