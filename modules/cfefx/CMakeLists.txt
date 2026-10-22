# abcui.cfefx - cumulative damage / kill-streak feedback (see sdk/abcui/abi.h
# for the module build contract).
add_library(abcui_mod_cfefx MODULE src/CfefxModule.cpp)
target_include_directories(abcui_mod_cfefx PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_cfefx PROPERTIES OUTPUT_NAME "abcui_cfefx")
target_compile_definitions(abcui_mod_cfefx PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_cfefx PRIVATE /W4)
