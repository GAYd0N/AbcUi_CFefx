# abcui.textmenu - vanilla ShowMenu replacement module (see sdk/abcui/abi.h
# for the module build contract).
add_library(abcui_mod_textmenu MODULE src/TextMenuModule.cpp)
target_include_directories(abcui_mod_textmenu PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_textmenu PROPERTIES OUTPUT_NAME
                                                    "abcui_textmenu")
target_compile_definitions(abcui_mod_textmenu PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_textmenu PRIVATE /W4)
