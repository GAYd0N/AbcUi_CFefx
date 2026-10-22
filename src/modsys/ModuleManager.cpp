// framework::modmanager implementation - see ModuleManager.h.
#include "ModuleManager.h"

#include <map>
#include <set>
#include <string>

#include <Windows.h>

#include <abc/Console.h>
#include <abc/Paths.h>

#include "../modules/Module.h"
#include "ApiTable.h"
#include "EventBus.h"
#include "FrameworkVersion.h"
#include "MemoryDll.h"
#include "ModuleDocuments.h"
#include "ModuleManifest.h"
#include "ModuleVfs.h"

namespace framework {
namespace modmanager {
namespace {

constexpr const char *kEntrySymbol = "AbcUiModule_Main";

struct LoadedModule {
  ModuleManifest manifest;
  std::unique_ptr<MemoryDll> library;
  AbcUiModuleDescriptor descriptor{};
};

std::vector<LoadedModule> &loaded() {
  static std::vector<LoadedModule> instance;
  return instance;
}

std::vector<AbcUiPanelDesc> &pendingPanels() {
  static std::vector<AbcUiPanelDesc> instance;
  return instance;
}

int s_loadDepth = 0; // >0 while a descriptor onLoad may register panels

template <typename Fn> void guarded(const char *id, const char *what, Fn &&fn) {
  try {
    fn();
  } catch (...) {
    abc::ConsoleError("[AbcUi] module \"%s\": exception escaped %s\n", id,
                      what);
  }
}

struct Candidate {
  ModuleManifest manifest;
  std::string path;
  std::unique_ptr<PackageImage> image;
};

bool loadModule(Candidate &candidate) {
  const std::string &id = candidate.manifest.id;
  if (!ModuleVfs::mount(id.c_str(), std::move(candidate.image))) {
    abc::ConsoleWarn("[AbcUi] module \"%s\": package mount refused\n",
                     id.c_str());
    return false;
  }

  PackageEntry entryDll;
  std::string error;
  if (!ModuleVfs::openPath(id.c_str(), candidate.manifest.entry.c_str(),
                           entryDll, &error)) {
    abc::ConsoleWarn("[AbcUi] module \"%s\": entry DLL unreadable (%s)\n",
                     id.c_str(), error.c_str());
    ModuleVfs::unmount(id.c_str());
    return false;
  }

  auto library = std::make_unique<MemoryDll>();
  if (!library->load(entryDll.data, entryDll.size, &error)) {
    abc::ConsoleWarn("[AbcUi] module \"%s\": image load failed (%s)\n",
                     id.c_str(), error.c_str());
    ModuleVfs::unmount(id.c_str());
    return false;
  }

  const auto entry = reinterpret_cast<int (*)(uint32_t, const AbcUiModuleApi *,
                                              AbcUiModuleDescriptor *)>(
      library->getExport(kEntrySymbol));
  if (!entry) {
    abc::ConsoleWarn("[AbcUi] module \"%s\": no %s export\n", id.c_str(),
                     kEntrySymbol);
    ModuleVfs::unmount(id.c_str());
    return false;
  }

  AbcUiModuleDescriptor descriptor{};
  if (!entry(ABCUI_ABI_VERSION, ApiTable::get(), &descriptor) ||
      descriptor.structSize < sizeof(AbcUiModuleDescriptor) || !descriptor.id ||
      std::string(descriptor.id) != id) {
    abc::ConsoleWarn("[AbcUi] module \"%s\": entry refused or mismatched\n",
                     id.c_str());
    ModuleVfs::unmount(id.c_str());
    return false;
  }

  ++s_loadDepth;
  guarded(id.c_str(), "onLoad", descriptor.onLoad);
  --s_loadDepth;

  abc::ConsolePrint("[AbcUi] module \"%s\" loaded (%s)\n", id.c_str(),
                    candidate.manifest.version.c_str());
  LoadedModule module;
  module.manifest = candidate.manifest;
  module.library = std::move(library);
  module.descriptor = descriptor;
  loaded().push_back(std::move(module));
  return true;
}

// Single registered adapter: loaded modules ride the same spine points as
// the built-in modules (see modules/Module.h for the dispatch order).
class LoadedModulesAdapter final : public modules::IUiModule {
public:
  LoadedModulesAdapter() {
    modules::registry().add(this);
  }

  const char *name() const override {
    return "modmanager";
  }
  void onInit() override {
    modmanager::initAll();
  }
  void onContextReady(Rml::Context *context) override {
    modmanager::contextReadyAll(context);
  }
  void onFrame(double frameTime) override {
    modmanager::frameAll(frameTime);
  }
  void onVidInit() override {
    modmanager::vidInitAll();
  }
  void onShutdown() override {
    modmanager::shutdownAll();
  }
};

LoadedModulesAdapter s_adapter;

} // namespace

size_t boot() {
  static bool booted = false;
  if (booted)
    return loaded().size();
  booted = true;

  const std::string directory = abc::ModPath("abc/abcui/modules");
  const std::string pattern = directory + "\\*.zip";
  WIN32_FIND_DATAA find{};
  HANDLE handle = FindFirstFileA(pattern.c_str(), &find);
  if (handle == INVALID_HANDLE_VALUE)
    return 0; // no module directory: zero modules is a valid setup

  // Sorted by id: duplicate ids and the resolution order are deterministic.
  std::map<std::string, Candidate> candidates;
  std::string error;
  do {
    if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      continue;
    const std::string path = directory + "\\" + find.cFileName;
    auto image = PackageImage::open(path.c_str(), &error);
    if (!image) {
      abc::ConsoleWarn("[AbcUi] module package \"%s\" skipped: %s\n",
                       find.cFileName, error.c_str());
      continue;
    }
    PackageEntry manifestEntry;
    if (!image->file().read("manifest.json", manifestEntry, &error)) {
      abc::ConsoleWarn("[AbcUi] module package \"%s\" skipped: %s\n",
                       find.cFileName, error.c_str());
      continue;
    }
    Candidate candidate;
    if (!parseManifest(static_cast<const char *>(manifestEntry.data),
                       manifestEntry.size, candidate.manifest, &error)) {
      abc::ConsoleWarn("[AbcUi] module package \"%s\" skipped: %s\n",
                       find.cFileName, error.c_str());
      continue;
    }
    if (candidate.manifest.abi != ABCUI_ABI_VERSION) {
      abc::ConsoleWarn("[AbcUi] module \"%s\" skipped: abi %d, expected %d\n",
                       candidate.manifest.id.c_str(), candidate.manifest.abi,
                       ABCUI_ABI_VERSION);
      continue;
    }
    if (compareVersions(kFrameworkVersion, candidate.manifest.minAbcUi) < 0) {
      abc::ConsoleWarn("[AbcUi] module \"%s\" skipped: needs AbcUi >= %s\n",
                       candidate.manifest.id.c_str(),
                       candidate.manifest.minAbcUi.c_str());
      continue;
    }
    const std::string id = candidate.manifest.id;
    if (!candidates.emplace(id, std::move(candidate)).second) {
      abc::ConsoleWarn(
          "[AbcUi] module id \"%s\" is duplicated; first package wins\n",
          id.c_str());
    }
  } while (FindNextFileA(handle, &find));
  FindClose(handle);

  // Deterministic resolution: repeatedly load every module whose
  // dependencies are satisfied (id order), fail the ones whose dependencies
  // are missing/failed or whose conflicts are loaded. Whatever remains was
  // part of a dependency cycle.
  std::set<std::string> loadedIds;
  std::set<std::string> failedIds;
  bool progress = true;
  while (progress) {
    progress = false;
    for (auto &[id, candidate] : candidates) {
      if (loadedIds.count(id) || failedIds.count(id))
        continue;
      const ModuleManifest &manifest = candidate.manifest;

      bool blocked = false;
      for (const std::string &conflict : manifest.conflicts) {
        if (loadedIds.count(conflict)) {
          abc::ConsoleWarn(
              "[AbcUi] module \"%s\" skipped: conflicts with \"%s\"\n",
              id.c_str(), conflict.c_str());
          blocked = true;
          break;
        }
      }

      bool waiting = false;
      if (!blocked) {
        for (const std::string &dependency : manifest.dependencies) {
          if (loadedIds.count(dependency))
            continue;
          if (failedIds.count(dependency) || !candidates.count(dependency)) {
            abc::ConsoleWarn("[AbcUi] module \"%s\" skipped: dependency "
                             "\"%s\" is missing\n",
                             id.c_str(), dependency.c_str());
            blocked = true;
            break;
          }
          waiting = true; // exists but loads later; revisit next round
        }
      }

      if (blocked) {
        failedIds.insert(id);
        progress = true;
        continue;
      }
      if (waiting)
        continue;
      if (loadModule(candidate)) {
        loadedIds.insert(id);
      } else {
        failedIds.insert(id);
      }
      progress = true;
    }
  }
  for (const auto &[id, candidate] : candidates) {
    (void)candidate;
    if (!loadedIds.count(id) && !failedIds.count(id))
      abc::ConsoleWarn("[AbcUi] module \"%s\" skipped: dependency cycle\n",
                       id.c_str());
  }

  if (loadedIds.empty())
    abc::ConsolePrint("[AbcUi] no ui modules loaded\n");
  else
    abc::ConsolePrint("[AbcUi] %zu ui module(s) loaded\n", loaded().size());
  return loaded().size();
}

void initAll() {
  for (LoadedModule &module : loaded()) {
    if (module.descriptor.onInit)
      guarded(module.manifest.id.c_str(), "onInit", module.descriptor.onInit);
  }
}

void contextReadyAll(Rml::Context *context) {
  // Registered pages first: a module's onContextReady may already show one.
  module_documents::contextReady(context);
  for (LoadedModule &module : loaded()) {
    if (module.descriptor.onContextReady)
      guarded(module.manifest.id.c_str(), "onContextReady",
              [&] { module.descriptor.onContextReady(); });
  }
}

void frameAll(double frameTime) {
  // A degraded page's recovery click is honored before the modules project.
  module_documents::pump();
  for (LoadedModule &module : loaded()) {
    if (module.descriptor.onFrame)
      guarded(module.manifest.id.c_str(), "onFrame",
              [&] { module.descriptor.onFrame(frameTime); });
  }
}

void vidInitAll() {
  for (LoadedModule &module : loaded()) {
    if (module.descriptor.onVidInit)
      guarded(module.manifest.id.c_str(), "onVidInit",
              module.descriptor.onVidInit);
  }
}

void shutdownAll() {
  // Pages are framework-owned: close them first, while the Rml context is
  // alive and the modules are fully alive (onClose fires here, not after the
  // module's own teardown).
  module_documents::shutdown();
  for (auto it = loaded().rbegin(); it != loaded().rend(); ++it) {
    if (it->descriptor.onShutdown)
      guarded(it->manifest.id.c_str(), "onShutdown", it->descriptor.onShutdown);
  }
}

void unloadAll() {
  // Close pages before the modules tear themselves down (onClose is a module
  // callback); the descriptor strings and callback pointers point into the
  // module images, so the registry must also forget them before any image
  // goes away.
  module_documents::shutdown();
  for (auto it = loaded().rbegin(); it != loaded().rend(); ++it) {
    if (it->descriptor.onUnload)
      guarded(it->manifest.id.c_str(), "onUnload", it->descriptor.onUnload);
  }
  module_documents::unload();
  for (auto it = loaded().rbegin(); it != loaded().rend(); ++it)
    it->library->unload();
  loaded().clear();
  ModuleVfs::unmountAll();
}

bool acceptPanel(const AbcUiPanelDesc &desc) {
  if (s_loadDepth <= 0) {
    abc::ConsoleWarn(
        "[AbcUi] panel \"%s\" registration outside onLoad refused\n",
        desc.name);
    return false;
  }
  for (const AbcUiPanelDesc &accepted : pendingPanels()) {
    if (std::string(accepted.name) == desc.name) {
      abc::ConsoleWarn("[AbcUi] panel \"%s\" registered twice, refused\n",
                       desc.name);
      return false;
    }
  }
  pendingPanels().push_back(desc);
  return true;
}

std::vector<std::unique_ptr<HudPanel>> takePanels() {
  // The descriptors stay put: onLoad runs once per process but the panel
  // list is rebuilt after every HUD_Shutdown, so each install cycle gets
  // fresh wrappers over the same module-owned descriptor (its strings live
  // in the module image, which outlives every HUD cycle).
  std::vector<std::unique_ptr<HudPanel>> panels;
  panels.reserve(pendingPanels().size());
  for (const AbcUiPanelDesc &desc : pendingPanels())
    panels.push_back(std::make_unique<ModulePanel>(desc));
  return panels;
}

} // namespace modmanager
} // namespace framework
