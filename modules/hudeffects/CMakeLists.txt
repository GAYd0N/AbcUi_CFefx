# abcui.hudeffects - danger/water effect module (see sdk/abcui/abi.h for the
# module build contract).
add_library(abcui_mod_hudeffects MODULE src/HudEffectsModule.cpp)
target_include_directories(abcui_mod_hudeffects PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_hudeffects PROPERTIES OUTPUT_NAME
                                                      "abcui_hudeffects")
target_compile_definitions(abcui_mod_hudeffects PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_hudeffects PRIVATE /W4)
