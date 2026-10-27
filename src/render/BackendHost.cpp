// abc::render::BackendHost implementation - see BackendHost.h.
#include <metahook.h>

#include <windows.h>

#include <abc/Console.h>
#include <abc/CvarCmd.h>
#include <abc/Paths.h>
#include <abc/Plugin.h>

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "BackendHost.h"
#include "IRenderBackend.h"
#include <abcui/render_backend.h>

#include "../framework/FatalExit.h"
#include "../framework/NativeHandles.h"
#include "../services/UserConfig.h"

namespace abc::render {

namespace {

// ---------------------------------------------------------------- state ----

struct BackendEntry {
  std::string fileName; // inside the backend directory
  framework::NativeLibrary module;
  AbcUiRenderBackendDesc desc{};
  bool initialized = false;
};

struct HostState {
  bool booted = false;
  std::string bootId = "renderer";
  std::string configuredId = "renderer";
  std::string initializationError;
  std::vector<BackendEntry> entries;
  std::unique_ptr<struct TableBackend> active;
};

HostState &state() {
  static HostState instance;
  return instance;
}

// --------------------------------------------------------------- host API --

void ABCUI_CALL hostPrint(const char *format, ...) {
  char text[512];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  ConsolePrint("[AbcUi][render-backend] %s\n", text);
}

void ABCUI_CALL hostWarn(const char *format, ...) {
  char text[512];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  ConsoleWarn("[AbcUi][render-backend] %s\n", text);
}

void ABCUI_CALL hostError(const char *format, ...) {
  char text[512];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  state().initializationError = text;
  ConsoleError("[AbcUi][render-backend] %s\n", text);
}

void ABCUI_CALL hostEngineScreenSize(int32_t *width, int32_t *height) {
  SCREENINFO info{};
  info.iSize = sizeof(info);
  if (gEngfuncs.pfnGetScreenInfo)
    gEngfuncs.pfnGetScreenInfo(&info);
  if (width)
    *width = info.iWidth;
  if (height)
    *height = info.iHeight;
}

void *ABCUI_CALL hostPluginModule(const char *baseName) {
  if (!g_pMetaHookAPI || !baseName)
    return nullptr;
  mh_plugininfo_t info{};
  if (!g_pMetaHookAPI->GetPluginInfoByBaseFileName(baseName, &info))
    return nullptr;
  return info.PluginModuleBase;
}

AbcUiRenderBackendHost makeHost() {
  AbcUiRenderBackendHost host = {};
  host.structSize = sizeof(host);
  host.print = hostPrint;
  host.warn = hostWarn;
  host.error = hostError;
  host.engineScreenSize = hostEngineScreenSize;
  host.metahookApi = g_pMetaHookAPI;
  // Sys_GetFactory's real signature (CreateInterfaceFn (*)(HINTERFACEMODULE))
  // is host-side knowledge; the ABI carries it as an opaque cdecl fn-ptr.
  host.sysGetFactory =
      reinterpret_cast<void *(ABCUI_CALL *)(void *)>(Sys_GetFactory);
  host.pluginModule = hostPluginModule;
  return host;
}

// ---------------------------------------------------- table -> C++ bridge --

// Frame-sink trampolines: the ABI wants cdecl C pointers, the framework
// hands over an IFrameSink*.
IFrameSink *sinkInstance = nullptr;

void ABCUI_CALL sinkRenderFrame(void *) {
  if (sinkInstance)
    sinkInstance->renderFrame();
}
void ABCUI_CALL sinkInvalidateTargets(void *) {
  if (sinkInstance)
    sinkInstance->invalidateTargets();
}
void ABCUI_CALL sinkWorldPassBeat(void *) {
  if (sinkInstance)
    sinkInstance->worldPassBeat();
}

struct TableWorldPass final : IWorldPass {
  AbcUiRenderBackendDesc *desc = nullptr;

  unsigned int ensureTarget(int width, int height) override {
    return desc->worldEnsureTarget(desc->userdata, width, height);
  }
  void freeTarget() override {
    desc->worldFreeTarget(desc->userdata);
  }
  unsigned int texture() const override {
    return desc->worldTexture(desc->userdata);
  }
  void render(const Request &request) override {
    AbcUiWorldPassRequest pod = {};
    pod.structSize = sizeof(pod);
    pod.width = request.width;
    pod.height = request.height;
    for (int i = 0; i < 3; i++) {
      pod.origin[i] = request.origin[i];
      pod.angles[i] = request.angles[i];
    }
    pod.orthoHalfExtent = request.orthoHalfExtent;
    pod.orthoNear = request.orthoNear;
    pod.orthoFar = request.orthoFar;
    for (int i = 0; i < 4; i++)
      pod.clearColor[i] = request.clearColor[i];
    desc->worldRender(desc->userdata, &pod);
  }
};

struct TableBlurPass final : IBlurPass {
  AbcUiRenderBackendDesc *desc = nullptr;

  unsigned int render(const Request &request) override {
    AbcUiBlurPassRequest pod = {};
    pod.structSize = sizeof(pod);
    pod.width = request.width;
    pod.height = request.height;
    pod.sourceTexture = request.sourceTexture;
    pod.rounds = request.rounds;
    return desc->blurRender(desc->userdata, &pod);
  }
  void invalidate() override {
    desc->blurInvalidate(desc->userdata);
  }
};

struct TableTargets final : IOffscreenTargets {
  AbcUiRenderBackendDesc *desc = nullptr;

  unsigned int create(int width, int height) override {
    return desc->targetCreate(desc->userdata, width, height);
  }
  void free(unsigned int target) override {
    desc->targetFree(desc->userdata, target);
  }
  unsigned int texture(unsigned int target) const override {
    return desc->targetTexture(desc->userdata, target);
  }
  void clearViewportRegion(int x, int y, int width, int height,
                           const float rgba[4]) override {
    desc->clearViewportRegion(desc->userdata, x, y, width, height, rgba);
  }
  void copyViewportToTarget(unsigned int target, int x, int y, int width,
                            int height) override {
    desc->copyViewportToTarget(desc->userdata, target, x, y, width, height);
  }
};

struct TableBackend final : IRenderBackend {
  explicit TableBackend(AbcUiRenderBackendDesc &descriptor)
      : desc(&descriptor) {
    if (descriptor.capabilities & ABCUI_RB_CAP_WORLD_PASS)
      world.desc = &descriptor;
    if (descriptor.capabilities & ABCUI_RB_CAP_BLUR_PASS)
      blur.desc = &descriptor;
    if (descriptor.capabilities & ABCUI_RB_CAP_OFFSCREEN_TARGETS)
      targetsAdapter.desc = &descriptor;
  }

  const char *id() const override {
    return desc->id;
  }
  bool supports(Capability capability) const override {
    uint32_t bit = 0;
    switch (capability) {
    case Capability::RenderCallbacks:
      bit = ABCUI_RB_CAP_RENDER_CALLBACKS;
      break;
    case Capability::RedrawBeat:
      bit = ABCUI_RB_CAP_REDRAW_BEAT;
      break;
    case Capability::WorldPass:
      bit = ABCUI_RB_CAP_WORLD_PASS;
      break;
    case Capability::BlurPass:
      bit = ABCUI_RB_CAP_BLUR_PASS;
      break;
    case Capability::OffscreenTargets:
      bit = ABCUI_RB_CAP_OFFSCREEN_TARGETS;
      break;
    }
    return (desc->capabilities & bit) != 0;
  }
  int surfaceWidth() const override {
    return desc->surfaceWidth(desc->userdata);
  }
  int surfaceHeight() const override {
    return desc->surfaceHeight(desc->userdata);
  }

  bool installFrameSink(IFrameSink *sink) override {
    if (!desc->installFrameSink)
      return false;
    sinkInstance = sink;
    AbcUiRenderFrameSink pod = {};
    pod.structSize = sizeof(pod);
    pod.renderFrame = sinkRenderFrame;
    pod.invalidateTargets = sinkInvalidateTargets;
    pod.worldPassBeat = sinkWorldPassBeat;
    desc->installFrameSink(desc->userdata, &pod);
    return true;
  }
  void removeFrameSink() override {
    sinkInstance = nullptr;
    if (desc->removeFrameSink)
      desc->removeFrameSink(desc->userdata);
  }
  void pumpRedraw(double seconds) override {
    desc->pumpRedraw(desc->userdata, seconds);
  }

  void beginFrame() override {
    desc->beginFrame(desc->userdata);
  }
  void endFrame() override {
    desc->endFrame(desc->userdata);
  }
  void set2D(int x, int y, int width, int height) override {
    desc->set2D(desc->userdata, x, y, width, height);
  }
  void finish2D() override {
    desc->finish2D(desc->userdata);
  }
  void pushTranslation(float x, float y) override {
    desc->pushTranslation(desc->userdata, x, y);
  }
  void popTranslation() override {
    desc->popTranslation(desc->userdata);
  }
  void drawTextured(const Draw &draw) override {
    if (draw.vertexCount == 0 || draw.indexCount == 0)
      return;
    AbcUiRenderDraw pod = {};
    pod.structSize = sizeof(pod);
    pod.vertices = reinterpret_cast<const AbcUiRenderVertex *>(draw.vertices);
    pod.vertexCount = static_cast<uint32_t>(draw.vertexCount);
    pod.indices = draw.indices;
    pod.indexCount = static_cast<uint32_t>(draw.indexCount);
    pod.texture = draw.texture;
    pod.maskTexture = draw.maskTexture;
    switch (draw.mode) {
    case Blend::Additive:
      pod.blend = ABCUI_RB_BLEND_ADDITIVE;
      break;
    case Blend::PremultipliedAlpha:
      pod.blend = ABCUI_RB_BLEND_PREMULTIPLIED_ALPHA;
      break;
    default:
      pod.blend = ABCUI_RB_BLEND_STRAIGHT_ALPHA;
      break;
    }
    pod.label = draw.label;
    desc->drawTextured(desc->userdata, &pod);
  }
  void drawFilled(const FilledDraw &draw) override {
    if (draw.vertexCount == 0 || draw.indexCount == 0)
      return;
    AbcUiRenderFilledDraw pod = {};
    pod.structSize = sizeof(pod);
    pod.vertices =
        reinterpret_cast<const AbcUiRenderFilledVertex *>(draw.vertices);
    pod.vertexCount = static_cast<uint32_t>(draw.vertexCount);
    pod.indices = draw.indices;
    pod.indexCount = static_cast<uint32_t>(draw.indexCount);
    pod.blend = draw.mode == Blend::Additive ? ABCUI_RB_BLEND_ADDITIVE
                                             : ABCUI_RB_BLEND_STRAIGHT_ALPHA;
    pod.label = draw.label;
    desc->drawFilled(desc->userdata, &pod);
  }

  IWorldPass *worldPass() override {
    return world.desc ? &world : nullptr;
  }
  IBlurPass *blurPass() override {
    return blur.desc ? &blur : nullptr;
  }
  IOffscreenTargets *targets() override {
    return targetsAdapter.desc ? &targetsAdapter : nullptr;
  }

  AbcUiRenderBackendDesc *desc = nullptr;
  TableWorldPass world;
  TableBlurPass blur;
  TableTargets targetsAdapter;
};

// ------------------------------------------------------------ validation ---

#define ABCUI_REQUIRE_DESC(member)                                             \
  do {                                                                         \
    if (entry.desc.structSize <                                                \
        offsetof(AbcUiRenderBackendDesc, member) + sizeof(entry.desc.member))  \
      return "short structSize for " #member;                                  \
    if (!entry.desc.member)                                                    \
      return "missing " #member;                                               \
  } while (false)

// Optional-member presence: absent when the caller's structSize stops before
// it (reading past a short table would be out of bounds).
#define ABCUI_DESC_HAS(member)                                                 \
  (entry.desc.structSize >=                                                    \
       offsetof(AbcUiRenderBackendDesc, member) + sizeof(entry.desc.member) && \
   entry.desc.member != nullptr)

const char *validateDescriptor(BackendEntry &entry) {
  if (!entry.desc.id || !*entry.desc.id)
    return "missing id";
  for (const char *c = entry.desc.id; *c; c++)
    if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_'))
      return "id is not [a-z0-9_]+";
  const uint32_t caps = entry.desc.capabilities;
  if (!(caps & (ABCUI_RB_CAP_RENDER_CALLBACKS | ABCUI_RB_CAP_REDRAW_BEAT)))
    return "neither RENDER_CALLBACKS nor REDRAW_BEAT";
  if ((caps & ABCUI_RB_CAP_RENDER_CALLBACKS) &&
      (caps & ABCUI_RB_CAP_REDRAW_BEAT))
    return "RENDER_CALLBACKS and REDRAW_BEAT are exclusive";

  ABCUI_REQUIRE_DESC(initialize);
  ABCUI_REQUIRE_DESC(shutdown);
  ABCUI_REQUIRE_DESC(surfaceWidth);
  ABCUI_REQUIRE_DESC(surfaceHeight);
  ABCUI_REQUIRE_DESC(installFrameSink);
  ABCUI_REQUIRE_DESC(removeFrameSink);
  ABCUI_REQUIRE_DESC(pumpRedraw);
  ABCUI_REQUIRE_DESC(beginFrame);
  ABCUI_REQUIRE_DESC(endFrame);
  ABCUI_REQUIRE_DESC(set2D);
  ABCUI_REQUIRE_DESC(finish2D);
  ABCUI_REQUIRE_DESC(pushTranslation);
  ABCUI_REQUIRE_DESC(popTranslation);
  ABCUI_REQUIRE_DESC(drawTextured);
  ABCUI_REQUIRE_DESC(drawFilled);

  if (caps & ABCUI_RB_CAP_WORLD_PASS) {
    ABCUI_REQUIRE_DESC(worldEnsureTarget);
    ABCUI_REQUIRE_DESC(worldFreeTarget);
    ABCUI_REQUIRE_DESC(worldTexture);
    ABCUI_REQUIRE_DESC(worldRender);
  } else if (ABCUI_DESC_HAS(worldEnsureTarget) ||
             ABCUI_DESC_HAS(worldFreeTarget) || ABCUI_DESC_HAS(worldTexture) ||
             ABCUI_DESC_HAS(worldRender)) {
    return "world entries present without WORLD_PASS";
  }
  if (caps & ABCUI_RB_CAP_BLUR_PASS) {
    ABCUI_REQUIRE_DESC(blurRender);
    ABCUI_REQUIRE_DESC(blurInvalidate);
  } else if (ABCUI_DESC_HAS(blurRender) || ABCUI_DESC_HAS(blurInvalidate)) {
    return "blur entries present without BLUR_PASS";
  }
  if (caps & ABCUI_RB_CAP_OFFSCREEN_TARGETS) {
    ABCUI_REQUIRE_DESC(targetCreate);
    ABCUI_REQUIRE_DESC(targetFree);
    ABCUI_REQUIRE_DESC(targetTexture);
    ABCUI_REQUIRE_DESC(clearViewportRegion);
    ABCUI_REQUIRE_DESC(copyViewportToTarget);
  } else if (ABCUI_DESC_HAS(targetCreate) || ABCUI_DESC_HAS(targetFree) ||
             ABCUI_DESC_HAS(targetTexture) ||
             ABCUI_DESC_HAS(clearViewportRegion) ||
             ABCUI_DESC_HAS(copyViewportToTarget)) {
    return "target entries present without OFFSCREEN_TARGETS";
  }
  return nullptr;
}

bool idCharacterOk(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

std::string installedList() {
  std::string list;
  for (const auto &entry : state().entries) {
    if (!list.empty())
      list += ", ";
    list += entry.desc.id;
  }
  return list.empty() ? "(none)" : list;
}

// ------------------------------------------------------------------ boot ---

using BackendMainFn = int32_t(ABCUI_CALL *)(uint32_t,
                                            const AbcUiRenderBackendHost *,
                                            AbcUiRenderBackendDesc *);

void scanDirectory(const std::string &directory,
                   std::vector<std::string> &out) {
  out.clear();
  WIN32_FIND_DATAA data = {};
  const std::string pattern = directory + "\\*.dll";
  framework::NativeFindHandle find(FindFirstFileA(pattern.c_str(), &data));
  if (find.get() == INVALID_HANDLE_VALUE)
    return;
  do {
    out.push_back(data.cFileName);
  } while (FindNextFileA(find.get(), &data));
  find.reset();
  std::sort(out.begin(), out.end());
}

bool insideDirectory(const std::string &directory, HMODULE module) {
  char loaded[MAX_PATH]{};
  if (!GetModuleFileNameA(module, loaded, MAX_PATH))
    return false;
  // Case-insensitive prefix check on the canonical directory path.
  std::error_code error;
  const std::filesystem::path dir =
      std::filesystem::weakly_canonical(directory, error);
  if (error)
    return false;
  const std::filesystem::path file =
      std::filesystem::weakly_canonical(loaded, error);
  if (error)
    return false;
  const std::string dirStr = dir.string();
  const std::string fileStr = file.string();
  if (fileStr.size() < dirStr.size())
    return false;
  return _strnicmp(dirStr.c_str(), fileStr.c_str(), dirStr.size()) == 0;
}

void fatal(const std::string &reason) {
  framework::fatalExit(reason.c_str());
}

} // namespace

void boot() {
  HostState &hostState = state();
  if (hostState.booted)
    return;
  hostState.booted = true;

  std::string selected;
  if (!services::userconfig::renderBackend(selected)) {
    fatal("cannot read render_backend from abc/abcui/config.json; "
          "expected a backend id such as \"renderer\" or \"gl\"");
    return;
  }
  hostState.configuredId = selected;

  // 2. Scan the backend directory (deterministic: sorted by file name).
  std::error_code pathError;
  const std::string directory =
      std::filesystem::weakly_canonical(ModPath("abc/abcui/render_backend"),
                                        pathError)
          .string();
  if (pathError) {
    fatal("render backend \"" + selected +
          "\" cannot start: the backend directory could not be resolved "
          "(abc/abcui/render_backend)");
    return;
  }
  std::vector<std::string> files;
  scanDirectory(directory, files);
  if (files.empty()) {
    fatal("render backend \"" + selected + "\" is not installed: " + directory +
          " is empty. Install abcui_render_renderer.dll or "
          "abcui_render_gl.dll into "
          "abc/abcui/render_backend/");
    return;
  }

  // 3..5. Load every candidate, call the entry, validate the descriptor.
  const AbcUiRenderBackendHost host = makeHost();
  for (const std::string &file : files) {
    const std::string path = directory + "\\" + file;
    framework::NativeLibrary library(LoadLibraryExA(
        path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32));
    const auto module = library.get();
    if (!module) {
      ConsoleWarn("[AbcUi] render backend DLL not loaded: %s (GetLastError "
                  "%lu)\n",
                  file.c_str(), GetLastError());
      continue;
    }
    if (!insideDirectory(directory, module)) {
      // The search flags already pin the load location; this is the
      // belt-and-braces check from the issue's hardening list.
      ConsoleWarn("[AbcUi] render backend DLL rejected (outside the backend "
                  "directory): %s\n",
                  file.c_str());
      continue;
    }
    const auto entry = reinterpret_cast<BackendMainFn>(
        GetProcAddress(module, "AbcRenderBackend_Main"));
    if (!entry) {
      ConsoleWarn("[AbcUi] render backend DLL skipped (no "
                  "AbcRenderBackend_Main export): %s\n",
                  file.c_str());
      continue;
    }
    BackendEntry backend;
    backend.fileName = file;
    backend.desc.structSize = sizeof(backend.desc);
    const int32_t accepted =
        entry(ABCUI_RENDER_BACKEND_ABI_VERSION, &host, &backend.desc);
    const char *why = accepted ? validateDescriptor(backend)
                               : "entry returned 0 (ABI mismatch?)";
    if (why) {
      ConsoleWarn("[AbcUi] render backend DLL skipped (%s): %s\n", why,
                  file.c_str());
      continue;
    }
    // 6. Collect; a duplicate id is fatal (ambiguous selection).
    for (const auto &existing : hostState.entries) {
      if (std::string(existing.desc.id) == backend.desc.id) {
        fatal(std::string("render backend id \"") + backend.desc.id +
              "\" is provided by two DLLs: " + existing.fileName + ", " + file);
        return;
      }
    }
    backend.module = std::move(library);
    hostState.entries.push_back(std::move(backend));
  }

  // Missing Renderer is an availability fallback, not a reason to change
  // the user's saved preference. Installed-but-broken backends still report
  // their initialization failure rather than silently hiding it.
  const auto find = [&](const std::string &id) -> BackendEntry * {
    for (auto &entry : hostState.entries)
      if (id == entry.desc.id)
        return &entry;
    return nullptr;
  };
  BackendEntry *chosen = find(selected);
  if (selected == "renderer" && (!hostPluginModule("Renderer") || !chosen)) {
    ConsoleWarn("[AbcUi] renderer unavailable; falling back to gl\n");
    selected = "gl";
    chosen = find(selected);
  }
  if (!chosen) {
    fatal("render backend \"" + selected +
          "\" is not available "
          "(configured: " +
          hostState.configuredId + ", installed: " + installedList() + ")");
    return;
  }
  hostState.initializationError.clear();
  if (!chosen->desc.initialize(chosen->desc.userdata, &host)) {
    // Keep the backend's actionable reason visible in SysError too: the
    // console is not necessarily usable when startup fails.
    std::string reason =
        "render backend \"" + selected + "\" failed to initialize";
    if (!hostState.initializationError.empty())
      reason += ": " + hostState.initializationError;
    fatal(reason);
    return;
  }
  chosen->initialized = true;
  hostState.bootId = selected;
  hostState.active = std::make_unique<TableBackend>(chosen->desc);
  ConsolePrint("[AbcUi] render backend \"%s\" (%s) active\n", chosen->desc.id,
               chosen->desc.displayName ? chosen->desc.displayName
                                        : chosen->desc.id);
}

void shutdown() {
  HostState &hostState = state();
  if (hostState.active) {
    hostState.active->removeFrameSink();
  }
  for (auto &entry : hostState.entries) {
    if (entry.initialized && entry.desc.shutdown)
      entry.desc.shutdown(entry.desc.userdata);
    entry.initialized = false;
  }
  // Plugin teardown is the final backend consumer. Release wrappers before
  // their function tables and discard host callbacks before a later reload.
  hostState.active.reset();
  hostState.entries.clear();
  hostState.booted = false;
}

IRenderBackend *backend() {
  return state().active.get();
}

IWorldPass *worldPass() {
  return backend() ? backend()->worldPass() : nullptr;
}
IBlurPass *blurPass() {
  return backend() ? backend()->blurPass() : nullptr;
}
IOffscreenTargets *targets() {
  return backend() ? backend()->targets() : nullptr;
}

void pumpRedraw(double seconds) {
  if (IRenderBackend *active = backend())
    active->pumpRedraw(seconds);
}

// --------------------------------------------------------- console command --

namespace {

void listBackendsCommand() {
  const HostState &hostState = state();
  ConsolePrint("[AbcUi] render backends (configured: %s, active: %s):\n",
               hostState.configuredId.c_str(), hostState.bootId.c_str());
  for (const auto &entry : hostState.entries) {
    const uint32_t caps = entry.desc.capabilities;
    ConsolePrint("  %-10s %s [%s%s%s%s%s]\n", entry.desc.id,
                 entry.desc.displayName ? entry.desc.displayName : "",
                 caps & ABCUI_RB_CAP_RENDER_CALLBACKS ? "callbacks " : "",
                 caps & ABCUI_RB_CAP_REDRAW_BEAT ? "redraw " : "",
                 caps & ABCUI_RB_CAP_WORLD_PASS ? "world " : "",
                 caps & ABCUI_RB_CAP_BLUR_PASS ? "blur " : "",
                 caps & ABCUI_RB_CAP_OFFSCREEN_TARGETS ? "targets" : "");
  }
}

} // namespace

void registerCommands() {
  static bool registered = false;
  if (registered)
    return;
  registered = true;
  ADD_COMMAND("abc_ui_render_backend_list", listBackendsCommand);
}

} // namespace abc::render
