cmake_minimum_required(VERSION 4.2)

project(AbcUi LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/AbcCommon.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/MileFFmpeg.cmake")

# AbcCore is owned by the aggregate ABCEnchance repository. Standalone builds
# can use a sibling checkout or pass -DABC_CORE_DIR=<path>.
set(ABC_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../AbcCore" CACHE PATH
    "Path to the shared AbcCore source tree")
if(NOT EXISTS "${ABC_CORE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "AbcCore not found at '${ABC_CORE_DIR}'. Clone DrAbcOfficial/AbcCore as a sibling of AbcUi or pass -DABC_CORE_DIR=<path>.")
endif()
add_subdirectory("${ABC_CORE_DIR}" AbcCore-build)

# AbcUi and AbcCore.dll must share one CRT heap. Pin the core target
# explicitly so a sibling or aggregate-source checkout always builds /MD.
set_target_properties(abc_core PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Renderer public interfaces are a direct AbcUi dependency. AbcCore deliberately
# stays renderer-free so non-UI consumers do not inherit the Renderer checkout.
set(ABCUI_RENDERER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/Renderer")
set(ABCUI_RENDERER_INCLUDE_DIR "${ABCUI_RENDERER_DIR}/include")
if(NOT EXISTS "${ABCUI_RENDERER_INCLUDE_DIR}/Interface/IMetaRenderer.h")
    message(FATAL_ERROR "AbcUi requires thirdparty/Renderer; initialize the submodule with git submodule update --init --recursive")
endif()

# Mile.FFmpeg binary SDK (video decode). Headers only in the build: the
# distance to Mile.FFmpeg.dll is GetProcAddress, exactly like Renderer's
# startup video. MetaHookSV/Renderer owns the runtime DLL; AbcUi only uses the
# headers and never packages the DLL.
abcui_prepare_mile_ffmpeg(ABCUI_MILE_FFMPEG_SDK)

set(METAHOOK_INCLUDE_DIR "${ABC_CORE_DIR}/thirdparty/MetaHook/include")
set(VGUI2EXTENSION_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/VGUI2Extension/include")
# Steamworks SDK (headers only - steamapi/ binds exports at runtime, no
# steam_api.lib import; see rmlui/SteamApiNative.cpp).
set(STEAMSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/SteamSDK")

# --- FreeType (R-3) ---------------------------------------------------------
# Static FreeType built from the pinned submodule. RmlUi locates its font
# engine through find_package(Freetype), which cannot see a build-tree target;
# the soft-dependency check in RmlUi's CMake/Dependencies.cmake only tests
# TARGET Freetype::Freetype. So: disable the search, add_subdirectory freetype
# and alias it to the name RmlUi links (rmlui_core PRIVATE Freetype::Freetype).
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_Freetype ON)
add_subdirectory(thirdparty/freetype EXCLUDE_FROM_ALL)
add_library(Freetype::Freetype ALIAS freetype)

# --- RmlUi 6.3 static --------------------------------------------------------
set(BUILD_SHARED_LIBS OFF)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/RmlUi EXCLUDE_FROM_ALL)

# --- FreeImage (R-5) ---------------------------------------------------------
# FreeImage_clone's CMake hardcodes a SHARED library, so the static tier of
# D-06 is not reachable; link its import lib. MetaHookSV/Renderer owns the
# runtime FreeImage.dll; AbcUi must not publish a second copy. MetaHook's
# launcher appends every metahook/dlls/* subdirectory to PATH, so the loader
# resolves the Renderer-provided DLL at runtime.
# Note: the vendored FreeImage CMake also forces /MT on its own objects (see
# the WebP note below). That stays an exception to the family-wide /MD rule:
# the DLL owns its heap and exposes a C-style API only, so no allocator,
# handle or C++ object crosses the boundary.
add_subdirectory(thirdparty/FreeImage EXCLUDE_FROM_ALL)

# --- Animated WebP decode backend -------------------------------------------
# FreeImage (like every still-image decoder behind it) only returns the first
# frame of an animated WebP, so the <animimg> element needs libwebp's demux +
# animation decoder. FreeImage's CMake builds its own LibWebP target with the
# static CRT (it switches the whole subtree to /MT) and does not export those
# symbols from FreeImage.dll, so the plugin compiles the decode half of the
# same pinned sources as its own /MD static library: one shared heap with
# AbcCore.dll, no second runtime DLL to ship. Only the decode side is linked:
# the linker pulls the archive members it needs and drops the encoder ones.
set(ABCUI_WEBP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/FreeImage/Source/LibWebP")
if(NOT EXISTS "${ABCUI_WEBP_DIR}/src/webp/demux.h")
    message(FATAL_ERROR
        "LibWebP sources missing at ${ABCUI_WEBP_DIR}; initialize thirdparty/FreeImage with git submodule update --init")
endif()
file(GLOB ABCUI_WEBP_SOURCES CONFIGURE_DEPENDS
    "${ABCUI_WEBP_DIR}/src/dec/*.c"
    "${ABCUI_WEBP_DIR}/src/demux/*.c"
    "${ABCUI_WEBP_DIR}/src/dsp/*.c"
    "${ABCUI_WEBP_DIR}/src/utils/*.c")
# Non-x86 SIMD variants compile to nothing under MSVC but only add build noise.
list(FILTER ABCUI_WEBP_SOURCES EXCLUDE REGEX "_(mips|mips_dsp_r2|neon|msa|dspr2)\\.c$")
add_library(abcui_webp STATIC ${ABCUI_WEBP_SOURCES})
# libwebp sources include each other from the LibWebP root ("src/dec/...").
target_include_directories(abcui_webp PUBLIC
    "${ABCUI_WEBP_DIR}" "${ABCUI_WEBP_DIR}/src")
target_compile_definitions(abcui_webp PRIVATE WIN32_LEAN_AND_MEAN WIN32 _WINDOWS)
set_target_properties(abcui_webp PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
# The VS generator leaves RuntimeLibrary empty for a C-only static library
# (the property alone does not reach the compiler here), which would make cl
# default to the static CRT. Pass the shared runtime explicitly.
target_compile_options(abcui_webp PRIVATE
    $<$<CONFIG:Debug>:/MDd>
    $<$<NOT:$<CONFIG:Debug>>:/MD>)

# --- miniz (module package reader) -------------------------------------------
# Read-only zip support for UI module packages. Memory-image reads only: the
# writer, stdio and time-conversion surfaces are compiled out.
set(ABCUI_MINIZ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/miniz")
add_library(abcui_miniz STATIC
    "${ABCUI_MINIZ_DIR}/miniz.c"
    "${ABCUI_MINIZ_DIR}/miniz_tdef.c"
    "${ABCUI_MINIZ_DIR}/miniz_tinfl.c"
    "${ABCUI_MINIZ_DIR}/miniz_zip.c")
target_include_directories(abcui_miniz PUBLIC "${ABCUI_MINIZ_DIR}")
target_compile_definitions(abcui_miniz PRIVATE
    MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_WRITING_APIS)
# miniz 3.x includes a generated visibility header; a static build only needs
# MINIZ_EXPORT to expand to nothing (what upstream CMake does for static libs).
set(ABCUI_MINIZ_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/miniz-gen")
file(WRITE "${ABCUI_MINIZ_GEN_DIR}/miniz_export.h"
    "#pragma once\n#ifndef MINIZ_EXPORT\n#define MINIZ_EXPORT\n#endif\n")
target_include_directories(abcui_miniz PUBLIC "${ABCUI_MINIZ_GEN_DIR}")
set_target_properties(abcui_miniz PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
target_compile_options(abcui_miniz PRIVATE
    $<$<CONFIG:Debug>:/MDd>
    $<$<NOT:$<CONFIG:Debug>>:/MD>)

add_library(abc_ui_config STATIC
    src/services/uiconfig/UiDefinitions.cpp
    src/services/uiconfig/ConfigRepository.cpp
    src/services/commandmenu/CommandMenuModel.cpp
    src/services/commandmenu/CommandMenuService.cpp
    src/services/serverbrowser/ServerBlacklist.cpp
    src/services/serverbrowser/BlacklistFileStore.cpp)
target_include_directories(abc_ui_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${ABC_CORE_DIR}/thirdparty/MetaHook/thirdparty/rapidjson/include")
target_compile_features(abc_ui_config PUBLIC cxx_std_20)

add_library(AbcUi MODULE
    src/plugins.cpp
    src/CoreGlobals.cpp
    src/hooks.cpp
    src/modsys/EventBus.cpp
    src/modsys/ModuleResources.cpp
    src/modsys/PackageFile.cpp
    src/modsys/PackageImage.cpp
    src/modsys/ModuleVfs.cpp
    src/modsys/MemoryDll.cpp
    src/modsys/ModuleManifest.cpp
    src/modsys/ModulePanel.cpp
    src/modsys/ApiTable.cpp
    src/modsys/ModuleManager.cpp
    src/modsys/ApiAudio.cpp
    src/modsys/ApiTextures.cpp
    src/services/media/FfmpegApi.cpp
    src/modsys/ApiSections.cpp
    src/modsys/ApiWeapons.cpp
    src/modsys/ApiPlayers.cpp
    src/modsys/ApiServices.cpp
    src/modsys/ApiInput.cpp
    src/modsys/ApiDocuments.cpp
    src/modsys/ApiMenu.cpp
    src/modsys/ApiVoteMenu.cpp
    src/modsys/ApiPlayerList.cpp
    src/modsys/ApiCommandMenu.cpp
    src/modsys/ApiMenuConfig.cpp
    src/modsys/ApiSocial.cpp
    src/modsys/ApiInventory.cpp
    src/modsys/ApiCreateGame.cpp
    src/modsys/ApiConsolePage.cpp
    src/modsys/ApiServerBrowser.cpp
    src/modsys/ApiConnection.cpp
    src/modsys/ApiSettings.cpp
    src/modsys/ModuleDocuments.cpp
    src/modsys/ModuleServices.cpp
    src/render/Render.cpp
    src/render/RadarRender.cpp
    src/render/ScreenMetrics.cpp
    src/rmlui/AbcRenderInterface.cpp
    src/rmlui/model/ElementModel.cpp
    src/rmlui/colorpicker/ElementColorPicker.cpp
    src/rmlui/backdrop/ElementBackdrop.cpp
    src/rmlui/video/ElementVideo.cpp
    src/rmlui/animated/AnimatedImage.cpp
    src/rmlui/animated/ApngDecoder.cpp
    src/rmlui/animated/WebPDecoder.cpp
    src/rmlui/animated/ElementAnimatedImage.cpp
    src/rmlui/backdrop/Backdrop.cpp
    src/services/friends/FriendsModel.cpp
    src/services/friends/FriendsSteam.cpp
    src/rmlui/modelpreview/Options.cpp
    src/rmlui/modelpreview/Target.cpp
    src/services/video/Decoder.cpp
    src/services/video/AsyncDecoder.cpp
    src/services/video/Options.cpp
    src/rmlui/video/VideoTexture.cpp
    src/rmlui/texture/DynamicTexture.cpp
    src/services/spray/Encoder.cpp
    src/modules/modeltest/ModelTest.cpp
    src/modules/videotest/VideoTest.cpp
    src/rmlui/AbcSystemInterface.cpp
    src/rmlui/InputBridge.cpp
    src/rmlui/KeyMap.cpp
    src/rmlui/NativeEntryCallbacks.cpp
    src/rmlui/PaintGate.cpp
    src/rmlui/RmlUiModule.cpp
    src/modules/gameui/debug/ProbeCommand.cpp
    src/rmlui/ImeOverlay.cpp
    src/rmlui/TextInput.cpp
    src/rmlui/TextureChannel.cpp
    src/rmlui/LanguageChannel.cpp
    src/rmlui/SourceSpewStub.cpp
    src/services/steam/SteamAPI.cpp
    src/services/steam/SteamApiBridge.cpp
    src/services/steam/SteamApiNative.cpp
    src/rmlui/VGUI2ExtensionImport.cpp
    src/framework/ResourceFile.cpp
    src/framework/ShellActions.cpp
    src/framework/Commands.cpp
    src/framework/FatalExit.cpp
    src/framework/TextEncoding.cpp
    src/framework/HudPanel.cpp
    src/framework/ScoreSeat.cpp
    src/framework/ImageLoader.cpp
    src/framework/MenuBackdrop.cpp
    src/framework/PreviewImage.cpp
    src/framework/InteractiveDocument.cpp
    src/framework/UiInputRouter.cpp
    src/framework/Localization.cpp
    src/rmlui/texture/Avatars.cpp
    src/data/HudState.cpp
    src/data/NetworkMessages.cpp
    src/data/PlayerResource.cpp
    src/data/PlayerTrace.cpp
    src/rmlui/texture/SpriteResource.cpp
    src/rmlui/texture/SpriteUri.cpp
    src/data/TextFeed.cpp
    src/data/TeamResource.cpp
    src/data/WeaponResource.cpp
    src/modules/UiModuleRegistry.cpp
    src/modules/gameui/GameUiModule.cpp
    src/modules/gameui/lifecycle/Frame.cpp
    src/modules/gameui/lifecycle/BackgroundWork.cpp
    src/modules/gameui/lifecycle/PresentationFrame.cpp
    src/modules/gameui/lifecycle/Documents.cpp
    src/modules/gameui/navigation/MenuPresentation.cpp
    src/modules/gameui/navigation/PageProjection.cpp
    src/modules/gameui/navigation/Actions.cpp
    src/modules/gameui/navigation/MenuAction.cpp
    src/modules/gameui/input/MenuInput.cpp
    src/modules/gameui/settings/SprayMaker.cpp
    src/modules/gameui/settings/VideoFlow.cpp
    src/modules/gameui/serverbrowser/ServerBrowserFlow.cpp
    src/modules/gameui/creategame/CreateGameFlow.cpp
    src/modules/gameui/votemenu/VoteMenuFlow.cpp
    src/modules/gameui/commandmenu/CommandMenuFlow.cpp
    src/modules/gameui/inventory/InventoryFlow.cpp
    src/modules/gameui/connection/LoadingFlow.cpp
    src/modules/gameui/connection/ConnectionActions.cpp
    src/modules/gameui/debug/DebugCommands.cpp
    src/modules/gameui/debug/SettingsCommands.cpp
    src/modules/gameui/debug/VideoCommands.cpp
    src/modules/gameui/debug/LoadingCommands.cpp
    src/modules/gameui/debug/ConsoleCommands.cpp
    src/modules/gameui/debug/NavigationCommands.cpp
    src/modules/gameui/debug/ServerCommands.cpp
    src/modules/gameui/debug/CreategameCommands.cpp
    src/modules/gameui/debug/PlayerlistCommands.cpp
    src/modules/gameui/engine/MenuBindings.cpp
    src/modules/gameui/engine/SettingsBindings.cpp
    src/modules/gameui/engine/VideoBindings.cpp
    src/modules/gameui/settings/SettingsResources.cpp
    src/modules/gameui/navigation/MenuController.cpp
    src/services/GameUiBridge.cpp
    src/services/menu/MenuHost.cpp
    src/services/uiconfig/MenuShellService.cpp
    src/services/friends/FriendsService.cpp
    src/services/votemenu/VoteMenuService.cpp
    src/services/steam/RuntimeAccess.cpp
    src/services/EngineUiBackend.cpp
    src/services/console/ConsoleBuffer.cpp
    src/services/console/ConsoleBackend.cpp
    src/services/console/ConsolePageService.cpp
    src/services/serverbrowser/ServerQuery.cpp
    src/services/serverbrowser/ServerBrowserSession.cpp
    src/services/serverbrowser/ServerBrowserService.cpp
    src/services/serverbrowser/ServerQuerySteam.cpp
    src/services/serverbrowser/ServerListModel.cpp
    src/services/serverbrowser/ServerFavorites.cpp
    src/services/creategame/CreateGameModel.cpp
    src/services/creategame/CreateGameService.cpp
    src/services/creategame/ServerOptions.cpp
    src/services/playerlist/PlayerListModel.cpp
    src/services/playerlist/MuteBackend.cpp
    src/services/playerlist/PlayerListService.cpp
    src/services/votemenu/VoteMenuModel.cpp
    src/services/inventory/InventoryModel.cpp
    src/services/inventory/InventoryService.cpp
    src/services/loading/LoadingModel.cpp
    src/services/loading/AutoRetryGate.cpp
    src/services/loading/ConnectionService.cpp
    src/services/settings/SettingsModel.cpp
    src/services/settings/UserScrParser.cpp
    src/services/settings/ModelFavorites.cpp
    src/services/settings/KeyBindModel.cpp
    src/services/settings/VideoModeModel.cpp
    src/services/settings/VideoPendingStore.cpp
    src/services/settings/VoiceModel.cpp
    src/services/settings/SettingsPageService.cpp
    src/services/voice/VoiceBanStore.cpp
    src/services/voice/VoiceStatus.cpp
    src/services/crosshair/Geometry.cpp
    src/framework/PanelManager.cpp
    src/framework/PanelManagerLifecycle.cpp
    src/framework/Theme.cpp
    src/framework/Projection.cpp
    # Generic CreateInterface export (EXPOSE_SINGLE_INTERFACE implementation)
    "${METAHOOK_INCLUDE_DIR}/HLSDK/common/interface.cpp"
)
target_compile_definitions(AbcUi PRIVATE WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS NOMINMAX)
target_compile_options(AbcUi PRIVATE /W4)
target_compile_options(AbcUi PRIVATE /FIabc/ForceImport.h)
# The module SDK and the module manifest's JSON parser.
target_include_directories(AbcUi PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk"
    "${ABC_CORE_DIR}/thirdparty/MetaHook/thirdparty/rapidjson/include")
# imm32: T07 IME placement/cancel (ImmSetCompositionWindow / ImmNotifyIME).
target_link_libraries(AbcUi PRIVATE Abc::Core RmlUi::RmlUi FreeImage abcui_webp
    abcui_miniz imm32 abc_ui_config)

# First-party UI modules (each its own DLL + package; see modules/<id>/).
add_subdirectory(modules/demo)
add_subdirectory(modules/recovery)
add_subdirectory(modules/flashlight)
add_subdirectory(modules/textmenu)
add_subdirectory(modules/ammo)
add_subdirectory(modules/chat)
add_subdirectory(modules/crosshair)
add_subdirectory(modules/damage)
add_subdirectory(modules/cfefx)
add_subdirectory(modules/health)
add_subdirectory(modules/hudeffects)
add_subdirectory(modules/itemhighlight)
add_subdirectory(modules/playerboard)
add_subdirectory(modules/radar)
add_subdirectory(modules/scoreboard)
add_subdirectory(modules/spectator)
add_subdirectory(modules/killfeed)
add_subdirectory(modules/motd)
add_subdirectory(modules/mediaplayer)
add_subdirectory(modules/sidetext)
add_subdirectory(modules/textfeed)
add_subdirectory(modules/weaponchoose)
add_subdirectory(modules/vote)
add_subdirectory(modules/gameui)

# --- Module packages ---------------------------------------------------------
# Every modules/<id>/ CMakeLists registers one MODULE target. Collect them so
# the pack target builds the DLLs before zipping (a plain `--target dist`
# would otherwise ship stale or missing module entries).
set(ABCUI_MODULE_TARGETS "")
get_property(ABCUI_SUBDIRECTORIES DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    PROPERTY SUBDIRECTORIES)
foreach(ABCUI_SUBDIR IN LISTS ABCUI_SUBDIRECTORIES)
    if(ABCUI_SUBDIR MATCHES "/modules/")
        get_property(ABCUI_DIR_TARGETS DIRECTORY "${ABCUI_SUBDIR}"
            PROPERTY BUILDSYSTEM_TARGETS)
        list(APPEND ABCUI_MODULE_TARGETS ${ABCUI_DIR_TARGETS})
    endif()
endforeach()

set(ABCUI_MODULE_PACK_DIR "${CMAKE_BINARY_DIR}/dist/modules")
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_custom_target(pack_modules
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${ABCUI_MODULE_PACK_DIR}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/pack.py"
            --source-dir "${CMAKE_CURRENT_SOURCE_DIR}"
            --build-dir "${CMAKE_BINARY_DIR}"
            --config "$<CONFIG>"
            --out-dir "${ABCUI_MODULE_PACK_DIR}"
        DEPENDS AbcUi ${ABCUI_MODULE_TARGETS}
        COMMENT "Packing UI module packages")
else()
    message(WARNING "Python3 not found: the pack_modules and dist module "
        "packages will not be produced")
    add_custom_target(pack_modules
        COMMAND "${CMAKE_COMMAND}" -E echo
            "python3 not found: skipping module packages")
endif()
# The dist/deploy commands copy this directory unconditionally; keep it.
file(MAKE_DIRECTORY "${ABCUI_MODULE_PACK_DIR}")

# Renderer's gl_common constants are the source of truth for the render vertex
# layouts. Fail the build when AbcUi's copied table drifts from the pin.
add_custom_target(check-rendercommon-sync
    COMMAND "${CMAKE_COMMAND}"
        "-DABCUI_REPO_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_LIST_DIR}/cmake/CheckRenderCommonSync.cmake"
    COMMENT "Checking src/render/RenderCommon.h sync against thirdparty/Renderer"
    VERBATIM)
add_dependencies(AbcUi check-rendercommon-sync)

# Include layout mirrors Renderer's renderer_settings interface set: the
# VGUI2Extension interface headers include MetaHook VGUI/SourceSDK headers by
# bare name (<VGUI.h>, <Color.h>, <mathlib/vector2d.h>).
target_include_directories(AbcUi PRIVATE
    "${ABCUI_MILE_FFMPEG_SDK}/Include"
    # Resolves the /FIabc/ForceImport.h forced include explicitly instead of
    # relying on Abc::Core's transitive include directory.
    "${ABC_CORE_DIR}/include"
    "${STEAMSDK_DIR}"
    "${VGUI2EXTENSION_INCLUDE_DIR}"
    "${VGUI2EXTENSION_INCLUDE_DIR}/Interface"
    "${VGUI2EXTENSION_INCLUDE_DIR}/Interface/VGUI"
    "${ABCUI_RENDERER_INCLUDE_DIR}"
    "${ABCUI_RENDERER_INCLUDE_DIR}/Interface"
    "${METAHOOK_INCLUDE_DIR}/Interface"
    "${METAHOOK_INCLUDE_DIR}/Interface/VGUI"
    "${METAHOOK_INCLUDE_DIR}/VGUI"
    "${METAHOOK_INCLUDE_DIR}/SourceSDK"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(ABC_GAME_DIR)
    install(TARGETS AbcUi
        RUNTIME DESTINATION "${ABC_GAME_DIR}/svencoop/metahook/plugins"
        LIBRARY DESTINATION "${ABC_GAME_DIR}/svencoop/metahook/plugins")
    # Module packages produced by tools/pack.py (build a Release pack first).
    install(DIRECTORY "${ABCUI_MODULE_PACK_DIR}/"
        DESTINATION "${ABC_GAME_DIR}/svencoop/abc/abcui/modules")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/sound")
        # The cfefx score effect is engine-played: pfnPlaySoundByName prefixes
        # sound/, so the WAV lives outside the module packages.
        install(DIRECTORY assets/sound DESTINATION "${ABC_GAME_DIR}/svencoop")
    endif()
    add_custom_target(deploy
        DEPENDS AbcUi pack_modules
        COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config "$<CONFIG>"
        COMMENT "Deploying AbcUi.dll and assets to ${ABC_GAME_DIR}")
endif()

# --- Packaging ---------------------------------------------------------------
# `cmake --build <build-dir> --config Release --target dist` produces
# AbcUi-<version>.zip with the game root at the archive root, so it can be
# extracted directly into the Sven Co-op directory:
#   svencoop/metahook/plugins/AbcUi.dll
#   svencoop/abc/abcui/modules/*.zip (all UI resources)
#   svencoop/sound/abc/cfefx/*.wav (engine-played cfefx score effect)
#   README.md
# The target is named `dist` because `package` is reserved by the CPack
# configuration that thirdparty/FreeImage pulls in.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/modsys/FrameworkVersion.h" ABC_UI_VERSION_LINE
    REGEX "kFrameworkVersion")
string(REGEX MATCH "\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" ABC_UI_VERSION "${ABC_UI_VERSION_LINE}")
set(ABC_UI_VERSION "${CMAKE_MATCH_1}")
if(NOT ABC_UI_VERSION)
    message(WARNING "Could not parse the AbcUi version from src/modsys/FrameworkVersion.h; packaging as 0.0.0")
    set(ABC_UI_VERSION "0.0.0")
endif()
set(ABC_UI_STAGE_DIR "${CMAKE_BINARY_DIR}/dist/AbcUi-${ABC_UI_VERSION}")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/dist")
add_custom_target(dist
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${ABC_UI_STAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${ABC_UI_STAGE_DIR}/svencoop/metahook/plugins"
        "${ABC_UI_STAGE_DIR}/svencoop/abc"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:AbcUi>"
        "${ABC_UI_STAGE_DIR}/svencoop/metahook/plugins/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${ABC_UI_STAGE_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${ABC_UI_STAGE_DIR}/svencoop/abc/abcui/modules"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${ABCUI_MODULE_PACK_DIR}"
        "${ABC_UI_STAGE_DIR}/svencoop/abc/abcui/modules"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/sound"
        "${ABC_UI_STAGE_DIR}/svencoop/sound"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${ABC_UI_STAGE_DIR}"
        "${CMAKE_COMMAND}" -E tar cfv
        "${CMAKE_BINARY_DIR}/dist/AbcUi-${ABC_UI_VERSION}.zip"
        --format=zip -- svencoop README.md
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/dist"
    DEPENDS AbcUi pack_modules
    COMMENT "Packaging AbcUi-${ABC_UI_VERSION}.zip")
