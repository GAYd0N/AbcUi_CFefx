#pragma once
// Debug-only command facade. Not part of the runtime/lifecycle interface.
#ifndef NDEBUG
namespace gameui {
class DebugCommands {
public:
  static void registerCommands();

private:
  static void voiceStatusCommand();
  static void voiceBoostCommand();
  static void voiceTestCommand();
  static void voiceApplyCommand();
  static void settingsApplyCommand();
  static void settingsStatusCommand();
  static void bindStatusCommand();
  static void videoStatusCommand();
  static void videoScrollCommand();
  static void videoStageCommand();
  static void videoApplyCommand();
  static void videoKeepCommand();
  static void videoWindowCommand();
  static void videoRevertCommand();
  static void loadingConnectCommand();
  static void loadingCancelCommand();
  static void loadingPasswordCommand();
  static void loadingStatusCommand();
  static void consoleUiStatusCommand();
  static void consoleUiToggleCommand();
  static void consoleUiSubmitCommand();
  static void menuPageCommand();
  static void menuModelCommand();
  static void friendsTestCommand();
  static void settingsPageCommand();
  static void serverQueryCommand();
  static void serverStatusCommand();
  static void serverCancelCommand();
  static void serverReleaseCommand();
  static void serverDetailCommand();
  static void serverDetailCancelCommand();
  static void serverRefreshCommand();
  static void serverFavoriteCommand();
  static void serverHistoryCommand();
  static void serverConnectCommand();
  static void serverTimeoutCommand();
  static void serverSelectCommand();
  static void serverUiCommand();
  static void createGameUiCommand();
  static void createGameFillCommand();
  static void createGameApplyCommand();
  static void createGameProbeCommand();
  static void playerListUiCommand();
  static void playerListSelectCommand();
  static void playerListOpenCommand();
  static void playerListMuteCommand();
};
} // namespace gameui
#endif
