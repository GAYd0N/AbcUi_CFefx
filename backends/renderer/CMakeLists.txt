# abcui_render_renderer - the Renderer.dll-backed render backend (issue #24).
# A plain LoadLibrary DLL behind the sdk/abcui/render_backend.h C ABI: it
# links neither AbcCore nor any framework static library, so the include
# directories it needs are spelled out here instead of inherited from AbcUi.

add_library(abcui_render_renderer MODULE RendererBackend.cpp)
target_compile_definitions(abcui_render_renderer PRIVATE
    WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS NOMINMAX)
target_compile_features(abcui_render_renderer PRIVATE cxx_std_20)
target_include_directories(abcui_render_renderer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/sdk"
    "${ABCUI_RENDERER_INCLUDE_DIR}"
    "${ABCUI_RENDERER_INCLUDE_DIR}/Interface"
    "${METAHOOK_INCLUDE_DIR}"
    "${METAHOOK_INCLUDE_DIR}/Interface"
    "${METAHOOK_INCLUDE_DIR}/VGUI"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/common"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/cl_dll"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/engine"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/pm_shared"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/public")
# opengl32: the viewport capture ops (scissor clear + copy-out) are the two
# fixed-pipeline niches with no IMetaRenderer equivalent.
target_link_libraries(abcui_render_renderer PRIVATE opengl32)
set_target_properties(abcui_render_renderer PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
