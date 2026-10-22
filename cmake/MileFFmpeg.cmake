include_guard(GLOBAL)

# Mile.FFmpeg binary SDK discovery. Mirrors Renderer's cmake/MileFFmpeg.cmake
# (the module the user pointed at): the SDK is a shared binary redistribution,
# not an import library, so the plugin only consumes its headers and loads
# Mile.FFmpeg.dll at runtime (services/video/Decoder.cpp).
#
# Search order:
#   1. ABCUI_MILE_FFMPEG_ROOT / MILE_FFMPEG_ROOT (explicit cache / env)
#   2. the sibling MetaHookSv Renderer checkout's already-fetched SDK (dev
#      tree; only with ABCUI_ALLOW_LOCAL_MILE_FFMPEG, because a local tree is
#      not covered by the pinned release hash)
#   3. FetchContent of the pinned release (same URL/hash as Renderer)
set(ABCUI_MILE_FFMPEG_ROOT "$ENV{MILE_FFMPEG_ROOT}" CACHE PATH
    "Mile.FFmpeg 6.1.0.0 binary SDK; empty downloads the verified release")
option(ABCUI_ALLOW_LOCAL_MILE_FFMPEG
    "Accept an unverified local Mile.FFmpeg tree (development only)" OFF)

function(abcui_prepare_mile_ffmpeg out_root)
    set(root "${ABCUI_MILE_FFMPEG_ROOT}")
    if(NOT root AND ABCUI_ALLOW_LOCAL_MILE_FFMPEG)
        foreach(candidate
                "${CMAKE_CURRENT_SOURCE_DIR}/../MetaHookSv/Plugins/Renderer/build/preview/_deps/renderer_mile_ffmpeg-src"
                "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/MileFFmpeg")
            if(NOT root AND EXISTS "${candidate}/Include/libavcodec/avcodec.h")
                get_filename_component(root "${candidate}" ABSOLUTE)
            endif()
        endforeach()
    endif()
    if(root)
        get_filename_component(root "${root}" ABSOLUTE)
    else()
        include(FetchContent)
        if(POLICY CMP0135)
            cmake_policy(SET CMP0135 NEW)
        endif()
        FetchContent_Declare(abcui_mile_ffmpeg
            URL https://github.com/ProjectMileArchived/Mile.FFmpeg/releases/download/6.1.0.0/Mile.FFmpeg.zip
            URL_HASH SHA256=17425bd7cd737aa956daa9d6cb3cdff9b05fe6269634b06981e78f331dfd0272
            SOURCE_SUBDIR Include)
        FetchContent_MakeAvailable(abcui_mile_ffmpeg)
        set(root "${abcui_mile_ffmpeg_SOURCE_DIR}")
    endif()
    foreach(file Include/libavcodec/avcodec.h Include/libavformat/avformat.h
                 Include/libswscale/swscale.h Redist/x86/Mile.FFmpeg.dll)
        if(NOT EXISTS "${root}/${file}")
            message(FATAL_ERROR "Mile.FFmpeg SDK is missing ${file}: ${root}")
        endif()
    endforeach()
    set(${out_root} "${root}" PARENT_SCOPE)
endfunction()
