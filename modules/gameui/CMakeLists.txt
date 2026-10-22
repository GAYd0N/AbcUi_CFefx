# abcui.gameui - the replacement game-menu pages (see sdk/abcui/abi.h for the
# module build contract). Every menu page lives here: the shell, the console,
# the server browser, the connection surfaces and the vote creation, player
# list, command menu, inventory, create-game, settings, display-mode
# confirmation and quit-confirmation pages. PageShared.h carries the borrowed
# api table and the shared text/DOM helpers.
add_library(abcui_mod_gameui MODULE
            src/GameUiModule.cpp
            src/MainMenuPage.cpp
            src/VoteMenuPage.cpp
            src/PlayerListPage.cpp
            src/CommandMenuPage.cpp
            src/InventoryPage.cpp
            src/CreateGamePage.cpp
            src/ConsolePage.cpp
            src/ServerBrowserPage.cpp
            src/ConnectionPage.cpp
            src/SettingsPage.cpp
            src/SettingsFileBrowser.cpp
            src/QuitConfirmPage.cpp)
target_include_directories(abcui_mod_gameui PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_gameui PROPERTIES OUTPUT_NAME "abcui_gameui")
target_compile_definitions(abcui_mod_gameui PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_gameui PRIVATE /W4)
