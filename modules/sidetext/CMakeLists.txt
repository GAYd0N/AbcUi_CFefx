# abcui.sidetext - side score/speed readout module (see sdk/abcui/abi.h for
# the module build contract).
add_library(abcui_mod_sidetext MODULE src/SideTextModule.cpp)
target_include_directories(abcui_mod_sidetext PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_sidetext PROPERTIES OUTPUT_NAME "abcui_sidetext")
target_compile_definitions(abcui_mod_sidetext PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_sidetext PRIVATE /W4)

target_link_libraries(abcui_mod_sidetext PRIVATE gdi32)
