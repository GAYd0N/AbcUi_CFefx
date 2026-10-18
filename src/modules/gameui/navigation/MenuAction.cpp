// Fixed action vocabulary shared by the module and its contract tests.
#include "MenuAction.h"
namespace gameui::navigation {
namespace {
struct Entry {
  const char *Name;
  Action Value;
};
constexpr Entry kActions[] = {
    {"resume", {ActionKind::kResume, ActionDomain::kNavigation}},
    {"disconnect", {ActionKind::kDisconnect, ActionDomain::kNavigation}},
    {"quit", {ActionKind::kQuit, ActionDomain::kNavigation}},
    {"quit-yes", {ActionKind::kQuitYes, ActionDomain::kNavigation}},
    {"quit-no", {ActionKind::kQuitNo, ActionDomain::kNavigation}},
    {"settings", {ActionKind::kSettings, ActionDomain::kNavigation}},
    {"servers", {ActionKind::kServers, ActionDomain::kNavigation}},
    {"creategame", {ActionKind::kCreateGame, ActionDomain::kNavigation}},
    {"cg-apply", {ActionKind::kCgApply, ActionDomain::kCreateGame}},
    {"cg-cancel", {ActionKind::kCgCancel, ActionDomain::kCreateGame}},
    {"playerlist", {ActionKind::kPlayerList, ActionDomain::kNavigation}},
    {"pl-mute", {ActionKind::kPlMute, ActionDomain::kPlayerList}},
    {"pl-close", {ActionKind::kPlClose, ActionDomain::kPlayerList}},
    {"votemenu", {ActionKind::kOpenVoteMenu, ActionDomain::kVoteMenu}},
    {"vm-submit", {ActionKind::kVoteMenuSubmit, ActionDomain::kVoteMenu}},
    {"vm-cancel", {ActionKind::kVoteMenuCancel, ActionDomain::kVoteMenu}},
    {"console", {ActionKind::kConsole, ActionDomain::kNavigation}},
    {"console-close", {ActionKind::kConsoleClose, ActionDomain::kNavigation}},
    // The console page's own layer reports these after it acted (the
    // submission already travelled through the submit sink); they are part of
    // the fixed vocabulary so the page's intents are never mistaken for an
    // unknown action - the console must stay free of AbcUi chatter.
    {"console-submit", {ActionKind::kConsoleSubmit, ActionDomain::kNavigation}},
    {"console-confirm-execute",
     {ActionKind::kConsoleConfirmExecute, ActionDomain::kNavigation}},
    {"console-confirm-cancel",
     {ActionKind::kConsoleConfirmCancel, ActionDomain::kNavigation}},
    {"directconnect", {ActionKind::kDirectConnect, ActionDomain::kConnection}},
    {"loading-cancel", {ActionKind::kLoadingCancel, ActionDomain::kConnection}},
    {"error-retry", {ActionKind::kErrorRetry, ActionDomain::kConnection}},
    {"error-return", {ActionKind::kErrorReturn, ActionDomain::kConnection}},
    {"directconnect-connect",
     {ActionKind::kDirectConnectConnect, ActionDomain::kConnection}},
    {"directconnect-cancel",
     {ActionKind::kDirectConnectCancel, ActionDomain::kConnection}},
    {"password-connect",
     {ActionKind::kPasswordConnect, ActionDomain::kConnection}},
    {"password-cancel",
     {ActionKind::kPasswordCancel, ActionDomain::kConnection}},
    {"return", {ActionKind::kReturn, ActionDomain::kNavigation}},
    {"close", {ActionKind::kClose, ActionDomain::kNavigation}},
    {"bind-capture-begin",
     {ActionKind::kBindCaptureBegin, ActionDomain::kNavigation}},
    {"sb-tab-internet", {ActionKind::kSbTabInternet, ActionDomain::kServers}},
    {"sb-tab-lan", {ActionKind::kSbTabLan, ActionDomain::kServers}},
    {"sb-tab-favorites", {ActionKind::kSbTabFavorites, ActionDomain::kServers}},
    {"sb-tab-history", {ActionKind::kSbTabHistory, ActionDomain::kServers}},
    {"sb-tab-friends", {ActionKind::kSbTabFriends, ActionDomain::kServers}},
    {"sb-filters-apply", {ActionKind::kSbFiltersApply, ActionDomain::kServers}},
    {"sb-refresh", {ActionKind::kSbRefresh, ActionDomain::kServers}},
    {"sb-stop", {ActionKind::kSbStop, ActionDomain::kServers}},
    {"sb-filter-empty", {ActionKind::kSbFilterEmpty, ActionDomain::kServers}},
    {"sb-filter-full", {ActionKind::kSbFilterFull, ActionDomain::kServers}},
    {"sb-filter-secure", {ActionKind::kSbFilterSecure, ActionDomain::kServers}},
    {"sb-connect", {ActionKind::kSbConnect, ActionDomain::kServers}},
    {"sb-favorite-toggle",
     {ActionKind::kSbFavoriteToggle, ActionDomain::kServers}},
    {"video-apply", {ActionKind::kVideoApply, ActionDomain::kVideo}},
    {"video-keep", {ActionKind::kVideoKeep, ActionDomain::kVideo}},
    {"video-revert", {ActionKind::kVideoRevert, ActionDomain::kVideo}},
};
} // namespace
Action parseAction(std::string_view Text) {
  for (const auto &Entry : kActions)
    if (Text == Entry.Name)
      return Entry.Value;
  return {};
}
const char *actionName(ActionKind Kind) {
  for (const auto &Entry : kActions)
    if (Kind == Entry.Value.Kind)
      return Entry.Name;
  return "unknown";
}
} // namespace gameui::navigation
