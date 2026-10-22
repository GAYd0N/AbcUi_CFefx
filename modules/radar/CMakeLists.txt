# abcui.radar - minimap plate module (see sdk/abcui/abi.h for the module build
# contract). The world-to-FBO producer stays in the framework
# (src/render/RadarRender.cpp).
add_library(abcui_mod_radar MODULE src/RadarModule.cpp)
target_include_directories(abcui_mod_radar PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_radar PROPERTIES OUTPUT_NAME "abcui_radar")
target_compile_definitions(abcui_mod_radar PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_radar PRIVATE /W4)
