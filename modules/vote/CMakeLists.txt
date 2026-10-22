# abcui.vote - yes/no vote callout module (see sdk/abcui/abi.h for the
# module build contract).
add_library(abcui_mod_vote MODULE src/VoteModule.cpp)
target_include_directories(abcui_mod_vote PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_vote PROPERTIES OUTPUT_NAME "abcui_vote")
target_compile_definitions(abcui_mod_vote PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_vote PRIVATE /W4)
