add_library(abcui_render_gl MODULE GlBackend.cpp)
target_compile_definitions(abcui_render_gl PRIVATE
    WIN32 _WINDOWS _USRDLL NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_features(abcui_render_gl PRIVATE cxx_std_20)
target_include_directories(abcui_render_gl PRIVATE "${CMAKE_SOURCE_DIR}/sdk")
target_link_libraries(abcui_render_gl PRIVATE opengl32)
set_target_properties(abcui_render_gl PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
