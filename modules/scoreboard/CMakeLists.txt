# abcui.scoreboard - +showscores table module (see sdk/abcui/abi.h for the
# module build contract). The interaction seat lives in the framework
# (src/framework/ScoreSeat.cpp).
add_library(abcui_mod_scoreboard MODULE src/ScoreboardModule.cpp)
target_include_directories(abcui_mod_scoreboard PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_scoreboard PROPERTIES OUTPUT_NAME
                                                       "abcui_scoreboard")
target_compile_definitions(abcui_mod_scoreboard PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_scoreboard PRIVATE /W4)
