# abcui.textfeed - centerprint/notify module (see sdk/abcui/abi.h for the
# module build contract).
add_library(abcui_mod_textfeed MODULE src/TextFeedModule.cpp)
target_include_directories(abcui_mod_textfeed PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_textfeed PROPERTIES OUTPUT_NAME "abcui_textfeed")
target_compile_definitions(abcui_mod_textfeed PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_textfeed PRIVATE /W4)
