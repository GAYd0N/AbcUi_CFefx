add_library(abcui_mod_recovery MODULE src/RecoveryModule.cpp)
target_include_directories(abcui_mod_recovery PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_recovery PROPERTIES OUTPUT_NAME "abcui_recovery")
target_compile_definitions(abcui_mod_recovery PRIVATE
    WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS NOMINMAX)
target_compile_options(abcui_mod_recovery PRIVATE /W4 /Gd)
