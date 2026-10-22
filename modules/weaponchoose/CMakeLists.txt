# abcui.weaponchoose - weapon selection bar module (see sdk/abcui/abi.h for
# the module build contract).
add_library(abcui_mod_weaponchoose MODULE src/WeaponChooseModule.cpp)
target_include_directories(abcui_mod_weaponchoose PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_weaponchoose PROPERTIES OUTPUT_NAME
                                                       "abcui_weaponchoose")
target_compile_definitions(abcui_mod_weaponchoose PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_weaponchoose PRIVATE /W4)
