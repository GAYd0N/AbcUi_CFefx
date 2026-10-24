# abcui.gameui - the replacement game-menu pages (see sdk/abcui/abi.h for the
# module build contract). One folder per surface: shell (the main menu and the
# quit confirmation), console, serverbrowser, connection, vote, playerlist,
# commandmenu, inventory, creategame and settings; shared/ carries the borrowed
# api table and the text/DOM helpers every page repeats, while GameUiModule.cpp
# is the entry that validates the table before any page sees it. The settings
# surface is further split by responsibility (rows, bindings, video, voice,
# colours, previews, model picker, spray picker, events, presentation) behind
# its own SettingsInternal.h seam.
add_library(abcui_mod_gameui MODULE
            src/GameUiModule.cpp
            src/shell/MainMenuPage.cpp
            src/shell/QuitConfirmPage.cpp
            src/console/ConsolePage.cpp
            src/serverbrowser/ServerBrowserPage.cpp
            src/serverbrowser/ProjectionSignature.cpp
            src/connection/ConnectionPage.cpp
            src/vote/VoteMenuPage.cpp
            src/playerlist/PlayerListPage.cpp
            src/commandmenu/CommandMenuPage.cpp
            src/inventory/InventoryPage.cpp
            src/creategame/CreateGamePage.cpp
            src/settings/SettingsPage.cpp
            src/settings/SettingsRows.cpp
            src/settings/SettingsBindings.cpp
            src/settings/SettingsVideo.cpp
            src/settings/SettingsVoice.cpp
            src/settings/SettingsColors.cpp
            src/settings/SettingsPreviews.cpp
            src/settings/SettingsModelPicker.cpp
            src/settings/SettingsSpray.cpp
            src/settings/SettingsEvents.cpp
            src/settings/SettingsPresentation.cpp
            src/settings/SettingsFileBrowser.cpp)
target_include_directories(abcui_mod_gameui PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_gameui PROPERTIES OUTPUT_NAME "abcui_gameui")
target_compile_definitions(abcui_mod_gameui PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_gameui PRIVATE /W4)
