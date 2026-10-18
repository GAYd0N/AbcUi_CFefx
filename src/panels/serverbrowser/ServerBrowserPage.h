#pragma once

// panels::serverbrowser - the T18 server browser page (one interactive
// document at the kServers menu page), replacing the T09 stub surface.
//
// The page is a PROJECTION of services::serverbrowser::ServerQuery (the T17
// data provider) through services::serverbrowser::ServerListModel (the
// T18a filter/sort/paging view model), exactly like the settings page
// projects its models: nothing here writes an engine value, never opens an
// original window, and no game state is inferred from the DOM. The gameui
// module owns visibility, the query lifecycle (begin/refresh/cancel on the
// user's explicit actions) and the tab truth; the page handles its own view
// state (row selection, sort, paging) against the models and reports fixed
// action intents through the sink - never user strings (T04 step 2).
//
// Rendering rules (T18a step 2/3):
//   - Every server-derived string reaches the DOM through
//     TextUtil::escapeRml (a hostile server name renders as text, never as
//     markup - the shipped list drew it through a plain vgui Label).
//   - Unknown facts stay visibly unknown: an entry without a response shows
//     "-" ping and "?" players - never a fabricated zero. The bounded
//     page window (kPageSize rows) is the render cap; paging reaches the rest.
//   - tick() is the per-frame beat: it syncs the view model when the snapshot
//     moved and rebuilds the DOM only when the projected signature changed
//     (including a digest over the visible rows - row values move while the
//     snapshot identity stays).
//
// Details: selecting a row issues the single-slot player query directly on
// the model (the model's own discipline replaces the previous detail request
// and drops stale generations - T17 step b; T18a step 3 "旧选中项响应不得
// 覆盖新详情"). The detail WINDOW renders the CURRENT slot only.
//
// 2026-10-15 redesign (four modules, main-menu theme):
//   - The filter column is docked at the right edge instead of a modal
//     subpage: text/ping controls apply client-side on change, the three
//     master-pair toggles report their change to the module (which re-issues
//     the list query), and the reset button restores the defaults.
//   - A LEFT click on a row selects it and opens the server detail window
//     (a modal subpage: the background is inert while it is up; ESC and the
//     Close button dismiss it - closeSubpage(). The window's own 加入/关闭
//     buttons live in its bottom row).
//   - A RIGHT click on a row selects it and opens the row's secondary menu
//     (join, favorite toggle, block address, block entire IP). The entries
//     reuse the fixed module intents (sb-connect / sb-favorite-toggle) and
//     the page-owned blacklist mutations (sb-block / sb-block-ip).
//   - The action bar reads 加入 / 自动重试 / 刷新 / 返回: join, the join
//     auto-retry toggle (the module re-enters the typed retry path when a
//     join fails), refresh (which reads as Stop while a list query is live)
//     and return. 自动重试 is page-owned view state, like sort and paging.
//   - The blacklist view is a table (name / address / blocked-at), the
//     Source dialog's shape.

#include "../../framework/InteractiveDocument.h"
#include "../../services/serverbrowser/ServerFavorites.h"
#include "../../services/serverbrowser/ServerQuery.h"
#include "../../services/serverbrowser/ServerListModel.h"

#include <RmlUi/Core/EventListener.h>

#include <functional>
#include <string>

namespace Rml {
class Element;
class Event;
} // namespace Rml

namespace services {
namespace serverbrowser {
class ServerListModel;
}
} // namespace services

namespace panels {
namespace serverbrowser {

// Same seam contract as the other menu windows: fixed page-internal action
// names only ("sb-tab-*", "sb-refresh", "sb-stop", "sb-filter-*",
// "sb-sort"/"sb-row"/"sb-page-*"/"sb-auto-retry"/"sb-detail-close"/
// "sb-filters-reset"/"sb-block*" resolved inside the page, "sb-connect"/
// "sb-favorite-toggle"/"return" reported to the module).
using ActionSink = std::function<void(const char *action)>;

class ServerBrowserPage final : public framework::InteractiveDocument {
public:
  ServerBrowserPage();
  ~ServerBrowserPage() override;

  void setActionSink(ActionSink sink) {
    Sink = std::move(sink);
  }
  void setQuery(services::serverbrowser::ServerQuery *query) {
    Query = query;
  }
  void setListModel(services::serverbrowser::ServerListModel *model) {
    List = model;
  }
  void setBlacklist(services::serverbrowser::ServerBlacklist *Value) {
    Blacklist = Value;
  }
  bool closeSubpage();
  // 自动重试 (the action bar's toggle): the module reads it when a join
  // attempt fails and re-enters the typed retry path itself; the page owns
  // the switch's truth (view state, like sort and paging).
  bool autoRetryJoin() const {
    return AutoRetryJoin;
  }
  // T18b: the authoritative favorites store (Steam) for the selected-row
  // state projection; never written from here - the module owns mutations
  // (the page only reports the toggle intent).
  void setFavorites(services::serverbrowser::IServerFavorites *favorites) {
    Favorites = favorites;
  }

  // Per-frame beat (module drives it while the page is presented): view
  // model sync when the snapshot moved, DOM rebuild when the projection
  // changed. Safe while hidden (no DOM touch when nothing moved).
  void tick();

  // Re-takes the safe default focus (module's per-frame focus guard): the
  // refresh button.
  void refocusDefault();

protected:
  void onDocumentReady() override;

private:
  // One delegated listener: click / mousedown / change on the document
  // (bubbling).
  class PageListener final : public Rml::EventListener {
  public:
    explicit PageListener(ServerBrowserPage *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    ServerBrowserPage *Owner;
  };

  void handleClick(Rml::Event &event);
  void handleMouseDown(Rml::Event &event);
  bool handleControls(Rml::Event &Event);
  void handleChange(Rml::Event &Event);
  void openContextMenu(int x, int y);
  void closeContextMenu();
  void closeDetails();
  void buildFilterOptions();
  void renderControls();
  void render();
  std::size_t projectionSignature() const;

  ActionSink Sink;
  services::serverbrowser::ServerQuery *Query = nullptr;
  services::serverbrowser::ServerListModel *List = nullptr;
  services::serverbrowser::IServerFavorites *Favorites = nullptr;
  services::serverbrowser::ServerBlacklist *Blacklist = nullptr;
  // The detail window is up (a modal subpage).
  bool DetailsOpen = false;
  // The row's secondary menu is up.
  bool MenuOpen = false;
  // 自动重试 enabled (the action bar toggle; off by default).
  bool AutoRetryJoin = false;
  bool InControls = false;
  std::string RuleStatus;
  PageListener Listener;
  // Whether the projection DOM matches the state (tick clears/re-sets it).
  bool Projected = false;
  // Last rendered player count of the detail slot (part of the signature;
  // kept implicit there, explicit here for cheap rebuilds).
  std::size_t LastSignature = 0;
  bool HaveSignature = false;
};

} // namespace serverbrowser
} // namespace panels
