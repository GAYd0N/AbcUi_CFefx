# abcui.mediaplayer - RmlUi replacement for the client's MediaPlayer window
# (see sdk/abcui/abi.h for the module build contract).
add_library(abcui_mod_mediaplayer MODULE src/MediaPlayerModule.cpp
                                        src/Mp3Duration.cpp)
target_include_directories(abcui_mod_mediaplayer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_mediaplayer PROPERTIES OUTPUT_NAME
                                                       "abcui_mediaplayer")
target_compile_definitions(abcui_mod_mediaplayer PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_mediaplayer PRIVATE /W4)
