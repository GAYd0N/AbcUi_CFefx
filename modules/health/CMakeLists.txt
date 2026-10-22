# abcui.health - health/armor readout module (see sdk/abcui/abi.h for the
# module build contract).
add_library(abcui_mod_health MODULE src/HealthModule.cpp)
target_include_directories(abcui_mod_health PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_health PROPERTIES OUTPUT_NAME "abcui_health")
target_compile_definitions(abcui_mod_health PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_health PRIVATE /W4)
