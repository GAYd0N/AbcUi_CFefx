# abcui.ammo - ammo readout and pickup stacks module (see sdk/abcui/abi.h for
# the module build contract).
add_library(abcui_mod_ammo MODULE src/AmmoModule.cpp)
target_include_directories(abcui_mod_ammo PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_ammo PROPERTIES OUTPUT_NAME "abcui_ammo")
target_compile_definitions(abcui_mod_ammo PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_ammo PRIVATE /W4)

target_link_libraries(abcui_mod_ammo PRIVATE gdi32)
