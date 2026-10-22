# abcui.itemhighlight - world-item highlight module (see sdk/abcui/abi.h for
# the module build contract).
add_library(abcui_mod_itemhighlight MODULE src/ItemHighlightModule.cpp)
target_include_directories(abcui_mod_itemhighlight PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_itemhighlight PROPERTIES OUTPUT_NAME
                                                           "abcui_itemhighlight")
target_compile_definitions(abcui_mod_itemhighlight PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_itemhighlight PRIVATE /W4)
