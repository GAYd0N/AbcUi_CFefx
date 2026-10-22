// Frozen Debug command registration.
#ifndef NDEBUG
#include "modules/gameui/GameUiModule.h"
#include "DebugCommands.h"
#include <abc/CvarCmd.h>
namespace gameui {
void DebugCommands::registerCommands() {

  // HUD_Init guarded block only (plan 08 §4.9 red line).
  abc::ADD_COMMAND("abc_ui_menu_page", menuPageCommand);
  abc::ADD_COMMAND("abc_ui_menu_model", menuModelCommand);
  abc::ADD_COMMAND("abc_ui_friends_test", friendsTestCommand);
  abc::ADD_COMMAND("abc_ui_settings_page", settingsPageCommand);
  abc::ADD_COMMAND("abc_ui_bind_status", bindStatusCommand);
  abc::ADD_COMMAND("abc_ui_settings_status", settingsStatusCommand);
  abc::ADD_COMMAND("abc_ui_settings_apply", settingsApplyCommand);
  abc::ADD_COMMAND("abc_ui_voice_status", voiceStatusCommand);
  abc::ADD_COMMAND("abc_ui_voice_boost", voiceBoostCommand);
  abc::ADD_COMMAND("abc_ui_voice_test", voiceTestCommand);
  abc::ADD_COMMAND("abc_ui_voice_apply", voiceApplyCommand);
  abc::ADD_COMMAND("abc_ui_video_status", videoStatusCommand);
  abc::ADD_COMMAND("abc_ui_video_scroll", videoScrollCommand);
  abc::ADD_COMMAND("abc_ui_video_stage", videoStageCommand);
  abc::ADD_COMMAND("abc_ui_video_apply", videoApplyCommand);
  abc::ADD_COMMAND("abc_ui_video_keep", videoKeepCommand);
  abc::ADD_COMMAND("abc_ui_video_revert", videoRevertCommand);
  abc::ADD_COMMAND("abc_ui_video_window", videoWindowCommand);
  abc::ADD_COMMAND("abc_ui_loading_connect", loadingConnectCommand);
  abc::ADD_COMMAND("abc_ui_loading_cancel", loadingCancelCommand);
  abc::ADD_COMMAND("abc_ui_loading_password", loadingPasswordCommand);
  abc::ADD_COMMAND("abc_ui_loading_status", loadingStatusCommand);
  abc::ADD_COMMAND("abc_ui_console_ui_status", consoleUiStatusCommand);
  abc::ADD_COMMAND("abc_ui_console_ui_toggle", consoleUiToggleCommand);
  abc::ADD_COMMAND("abc_ui_console_ui_submit", consoleUiSubmitCommand);
  abc::ADD_COMMAND("abc_ui_server_query", serverQueryCommand);
  abc::ADD_COMMAND("abc_ui_server_status", serverStatusCommand);
  abc::ADD_COMMAND("abc_ui_server_cancel", serverCancelCommand);
  abc::ADD_COMMAND("abc_ui_server_release", serverReleaseCommand);
  abc::ADD_COMMAND("abc_ui_server_detail", serverDetailCommand);
  abc::ADD_COMMAND("abc_ui_server_detail_cancel", serverDetailCancelCommand);
  abc::ADD_COMMAND("abc_ui_server_refresh", serverRefreshCommand);
  abc::ADD_COMMAND("abc_ui_server_timeout", serverTimeoutCommand);
  abc::ADD_COMMAND("abc_ui_server_ui", serverUiCommand);
  abc::ADD_COMMAND("abc_ui_server_select", serverSelectCommand);
  abc::ADD_COMMAND("abc_ui_server_favorite", serverFavoriteCommand);
  abc::ADD_COMMAND("abc_ui_server_history", serverHistoryCommand);
  abc::ADD_COMMAND("abc_ui_server_connect", serverConnectCommand);
  abc::ADD_COMMAND("abc_ui_create_game_ui", createGameUiCommand);
  abc::ADD_COMMAND("abc_ui_create_game_fill", createGameFillCommand);
  abc::ADD_COMMAND("abc_ui_create_game_apply", createGameApplyCommand);
  abc::ADD_COMMAND("abc_ui_create_game_probe", createGameProbeCommand);
  abc::ADD_COMMAND("abc_ui_playerlist_ui", playerListUiCommand);
  abc::ADD_COMMAND("abc_ui_playerlist_select", playerListSelectCommand);
  abc::ADD_COMMAND("abc_ui_playerlist_mute", playerListMuteCommand);
  abc::ADD_COMMAND("abc_ui_playerlist_open", playerListOpenCommand);
}
} // namespace gameui
#endif
