#pragma once
// DOM action decoder. Internal routing uses enums, never command strings.
#include <string_view>
namespace gameui::navigation {
enum class ActionDomain {
  kNavigation,
  kCreateGame,
  kPlayerList,
  kServers,
  kConnection,
  kVideo,
  kVoteMenu,
};
enum class ActionKind {
  kUnknown,
  kResume,
  kDisconnect,
  kQuit,
  kQuitYes,
  kQuitNo,
  kSettings,
  kServers,
  kCreateGame,
  kCgApply,
  kCgCancel,
  kPlayerList,
  kPlMute,
  kPlClose,
  kOpenVoteMenu,
  kVoteMenuSubmit,
  kVoteMenuCancel,
  kConsole,
  kConsoleClose,
  kConsoleSubmit,
  kConsoleConfirmExecute,
  kConsoleConfirmCancel,
  kDirectConnect,
  kLoadingCancel,
  kErrorRetry,
  kErrorReturn,
  kDirectConnectConnect,
  kDirectConnectCancel,
  kPasswordConnect,
  kPasswordCancel,
  kReturn,
  kClose,
  kBindCaptureBegin,
  kSbTabInternet,
  kSbTabLan,
  kSbTabFavorites,
  kSbTabHistory,
  kSbTabFriends,
  kSbFiltersApply,
  kSbRefresh,
  kSbStop,
  kSbFilterEmpty,
  kSbFilterFull,
  kSbFilterSecure,
  kSbConnect,
  kSbFavoriteToggle,
  kVideoApply,
  kVideoKeep,
  kVideoRevert,
};
struct Action {
  ActionKind Kind = ActionKind::kUnknown;
  ActionDomain Domain = ActionDomain::kNavigation;
};
Action parseAction(std::string_view Text);
const char *actionName(ActionKind Kind);
} // namespace gameui::navigation
