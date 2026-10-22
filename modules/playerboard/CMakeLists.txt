# abcui.playerboard - overhead teammate tile module (see sdk/abcui/abi.h for
# the module build contract).
add_library(abcui_mod_playerboard MODULE src/PlayerBoardModule.cpp)
target_include_directories(abcui_mod_playerboard PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_playerboard PROPERTIES OUTPUT_NAME
                                                        "abcui_playerboard")
target_compile_definitions(abcui_mod_playerboard PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_playerboard PRIVATE /W4)
