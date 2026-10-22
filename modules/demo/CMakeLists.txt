# abcui.demo - package-load smoke module (see sdk/abcui/abi.h for the module
# build contract). Development target only: the pack tool zips the DLL with
# manifest.json + assets; the demo package never ships in dist.
add_library(abcui_mod_demo MODULE src/DemoModule.cpp)
target_include_directories(abcui_mod_demo PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_demo PROPERTIES OUTPUT_NAME "abcui_demo")
target_compile_definitions(abcui_mod_demo PRIVATE
    WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS NOMINMAX)
target_compile_options(abcui_mod_demo PRIVATE /W4)
