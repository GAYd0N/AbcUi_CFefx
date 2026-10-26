// framework::modmanager implementation - see ModuleManager.h.
#include "ModuleManager.h"

#include <algorithm>
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
#include "ModuleResources.h"
#include "../framework/PanelManager.h"

namespace framework {
namespace modmanager {
namespace {

constexpr const char *kEntrySymbol = "AbcUiModule_Main";

struct LoadedModule {
  ModuleManifest manifest;
  std::shared_ptr<MemoryDll> library;
  module_resources::OwnerHandle owner;
  bool initialized = false;
  AbcUiModuleDescriptor descriptor{};
};

std::vector<LoadedModule> &loaded() {
  static std::vector<LoadedModule> instance;
  return instance;
}

size_t loadedCount() {
  return static_cast<size_t>(
      std::count_if(loaded().begin(), loaded().end(), [](const auto &module) {
        return module_resources::enabled(module.owner);
      }));
}

struct PanelRegistration {
  AbcUiPanelDesc desc;
  module_resources::Registration lifetime;
};

std::vector<PanelRegistration> &pendingPanels() {
  static std::vector<PanelRegistration> instance;
  return instance;
}

void retire(LoadedModule &module, bool failedInit = false) {
  module_resources::stop(module.owner);
  module.owner->initializing = false;
  if (failedInit && module.descriptor.onShutdown)
    module_resources::attempt(module.owner, "failed-init shutdown",
                              module.descriptor.onShutdown);
  if (module.descriptor.onUnload)
    module_resources::attempt(module.owner, "onUnload",
                              module.descriptor.onUnload);
  module_resources::release(module.owner);
  module.descriptor = {};
  module.owner->imagePin.reset();
  module.library.reset();
  ModuleVfs::unmount(module.manifest.id.c_str());
}

struct Candidate {
  ModuleManifest manifest;
  std::string path;
  std::unique_ptr<PackageImage> image;
};

bool loadModule(Candidate &candidate) {
  const std::string &id = candidate.manifest.id;
  LoadedModule module;
  module.manifest = candidate.manifest;
  module.owner = std::make_shared<module_resources::Owner>(id);
  module.owner->dependencies = candidate.manifest.dependencies;
  module.owner->initializing = true;
  if (!ModuleVfs::mount(id.c_str(), std::move(candidate.image),
                        candidate.manifest.dependencies)) {
    abc::ConsoleWarn("[AbcUi] module \"%s\": package mount refused\n",
                     id.c_str());
    return false;
  }
  if (module.manifest.resourcesOnly) {
    loaded().push_back(std::move(module));
    return true;
  }

  PackageEntry entryDll;
  std::string error;
  module.library = std::make_shared<MemoryDll>();
  if (!ModuleVfs::openPath(id.c_str(), candidate.manifest.entry.c_str(),
                           entryDll, &error) ||
      !module.library->load(entryDll.data, entryDll.size, &error)) {
    abc::ConsoleWarn("[AbcUi] module \"%s\": image load failed (%s)\n",
                     id.c_str(), error.c_str());
    retire(module);
    return false;
  }
  module.owner->imagePin = module.library;
  const auto entry = reinterpret_cast<AbcUiModuleEntry>(
      module.library->getExport(kEntrySymbol));
  AbcUiModuleDescriptor descriptor{};
  descriptor.structSize = sizeof(descriptor);
  int accepted = 0;
  const bool returned =
      entry && module_resources::attempt(module.owner, "AbcUiModule_Main", [&] {
        accepted = entry(ABCUI_ABI_VERSION, ApiTable::get(), &descriptor);
      });
  if (!returned || !accepted || module.owner->initializationFailed ||
      descriptor.structSize < sizeof(AbcUiModuleDescriptor) || !descriptor.id ||
      std::string(descriptor.id) != id) {
    abc::ConsoleWarn("[AbcUi] module \"%s\": entry refused or mismatched\n",
                     id.c_str());
    retire(module);
    return false;
  }
  module.descriptor = descriptor;
  module.owner->loading = true;
  const bool loadSucceeded =
      !descriptor.onLoad ||
      module_resources::attempt(module.owner, "onLoad", descriptor.onLoad);
  module.owner->loading = false;
  if (!loadSucceeded || module.owner->initializationFailed) {
    retire(module);
    return false;
  }
  abc::ConsolePrint("[AbcUi] module \"%s\" loaded (%s)\n", id.c_str(),
                    candidate.manifest.version.c_str());
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
    return loadedCount();
  booted = true;

  const std::string directory = abc::ModPath("abc/abcui/modules");
  const std::string pattern = directory + "\\*.zip";
  WIN32_FIND_DATAA find{};
  HANDLE handle = FindFirstFileA(pattern.c_str(), &find);
  if (handle == INVALID_HANDLE_VALUE)
    return 0; // no module directory: zero modules is a valid setup

  // One id may arrive in several packages: an in-place upgrade leaves the
  // previous version beside the new one. Enumeration order must never choose
  // which package provides an id, so validation runs first and the highest
  // manifest version wins; equal versions stay ambiguous and the id is
  // skipped entirely.
  std::map<std::string, Candidate> candidates;
  std::set<std::string> ambiguousIds;
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
    Candidate candidate;
    candidate.image = std::move(image);
    candidate.path = find.cFileName;
    PackageEntry manifestEntry;
    if (!candidate.image->file().read("manifest.json", manifestEntry, &error)) {
      abc::ConsoleWarn("[AbcUi] module package \"%s\" skipped: %s\n",
                       find.cFileName, error.c_str());
      continue;
    }
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
    const auto existing = candidates.find(id);
    if (existing == candidates.end()) {
      candidates.emplace(id, std::move(candidate));
      continue;
    }
    const int order = compareVersions(candidate.manifest.version,
                                      existing->second.manifest.version);
    if (order > 0) {
      abc::ConsoleWarn("[AbcUi] module id \"%s\" is packaged more than once; "
                       "using %s (\"%s\"), skipping %s (\"%s\")\n",
                       id.c_str(), candidate.manifest.version.c_str(),
                       candidate.path.c_str(),
                       existing->second.manifest.version.c_str(),
                       existing->second.path.c_str());
      existing->second = std::move(candidate);
    } else if (order < 0) {
      abc::ConsoleWarn("[AbcUi] module id \"%s\" is packaged more than once; "
                       "using %s (\"%s\"), skipping %s (\"%s\")\n",
                       id.c_str(), existing->second.manifest.version.c_str(),
                       existing->second.path.c_str(),
                       candidate.manifest.version.c_str(),
                       candidate.path.c_str());
    } else {
      abc::ConsoleWarn("[AbcUi] module id \"%s\" is duplicated at version %s; "
                       "all packages with this id skipped\n",
                       id.c_str(), candidate.manifest.version.c_str());
      candidates.erase(existing);
      ambiguousIds.insert(id);
    }
  } while (FindNextFileA(handle, &find));
  FindClose(handle);

  // A conflict is symmetric even when only one manifest declares it.
  std::map<std::string, std::set<std::string>> conflicts;
  for (const auto &[id, candidate] : candidates) {
    for (const std::string &other : candidate.manifest.conflicts) {
      conflicts[id].insert(other);
      conflicts[other].insert(id);
    }
  }

  // Deterministic resolution: repeatedly load every module whose
  // dependencies are satisfied (id order), fail the ones whose dependencies
  // are missing/failed or whose conflicts are loaded. Whatever remains was
  // part of a dependency cycle.
  std::set<std::string> loadedIds;
  std::set<std::string> failedIds = ambiguousIds;
  bool progress = true;
  while (progress) {
    progress = false;
    for (auto &[id, candidate] : candidates) {
      if (loadedIds.count(id) || failedIds.count(id))
        continue;
      const ModuleManifest &manifest = candidate.manifest;

      bool blocked = false;
      for (const std::string &conflict : conflicts[id]) {
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
  for (const auto &entry : candidates) {
    const std::string &id = entry.first;
    if (!loadedIds.count(id) && !failedIds.count(id))
      abc::ConsoleWarn("[AbcUi] module \"%s\" skipped: dependency cycle\n",
                       id.c_str());
  }

  if (loadedIds.empty())
    abc::ConsolePrint("[AbcUi] no ui modules loaded\n");
  else
    abc::ConsolePrint("[AbcUi] %zu ui module(s) loaded\n", loadedCount());
  return loadedCount();
}

void initAll() {
  for (LoadedModule &module : loaded()) {
    if (module.owner->initializationFailed && module.library) {
      retire(module);
      continue;
    }
    if (!module_resources::enabled(module.owner) || module.initialized)
      continue;
    bool dependencyFailed = false;
    for (const std::string &dependency : module.manifest.dependencies) {
      const auto parent = std::find_if(loaded().begin(), loaded().end(),
                                       [&](const LoadedModule &other) {
                                         return other.manifest.id == dependency;
                                       });
      if (parent == loaded().end() || !parent->initialized ||
          !module_resources::enabled(parent->owner)) {
        dependencyFailed = true;
        break;
      }
    }
    if (dependencyFailed) {
      abc::ConsoleWarn(
          "[AbcUi] module \"%s\": dependency initialization failed\n",
          module.manifest.id.c_str());
      retire(module);
      continue;
    }
    const bool initialized =
        !module.descriptor.onInit ||
        module_resources::attempt(module.owner, "onInit",
                                  module.descriptor.onInit);
    if (!initialized || module.owner->initializationFailed) {
      retire(module, true);
      continue;
    }
    module.initialized = true;
    module.owner->initializing = false;
  }
}

void contextReadyAll(Rml::Context *context) {
  module_documents::contextReady(context);
  for (LoadedModule &module : loaded())
    module_resources::invoke(module.owner, "onContextReady",
                             module.descriptor.onContextReady);
}

void frameAll(double frameTime) {
  module_documents::pump();
  for (LoadedModule &module : loaded())
    module_resources::invoke(module.owner, "onFrame", module.descriptor.onFrame,
                             frameTime);
}

void vidInitAll() {
  for (LoadedModule &module : loaded())
    module_resources::invoke(module.owner, "onVidInit",
                             module.descriptor.onVidInit);
}

void shutdownAll() {
  module_documents::shutdown();
  for (auto it = loaded().rbegin(); it != loaded().rend(); ++it) {
    module_resources::invoke(it->owner, "onShutdown",
                             it->descriptor.onShutdown);
    module_resources::releaseContext(it->owner);
  }
}

void unloadAll() {
  module_documents::shutdown();
  // Quiesce everyone before an unload callback can trigger another module.
  for (auto &module : loaded())
    module_resources::stop(module.owner);
  for (auto it = loaded().rbegin(); it != loaded().rend(); ++it)
    retire(*it);
  module_documents::unload();
  loaded().clear();
  ModuleVfs::unmountAll();
}

bool acceptPanel(const AbcUiPanelDesc &desc) {
  const auto owner = module_resources::current();
  if (!owner || !owner->loading || !module_resources::accepting()) {
    abc::ConsoleWarn(
        "[AbcUi] panel \"%s\" registration outside onLoad refused\n",
        desc.name);
    return false;
  }
  if (!desc.documentPath || !std::string_view(desc.documentPath)
                                 .starts_with("mod://" + owner->id + "/"))
    return false;
  for (const auto &accepted : pendingPanels()) {
    if (std::string(accepted.desc.name) == desc.name) {
      abc::ConsoleWarn("[AbcUi] panel \"%s\" registered twice, refused\n",
                       desc.name);
      return false;
    }
  }
  PanelRegistration registration{desc};
  const std::string name = desc.name;
  registration.lifetime.track([name] {
    std::erase_if(pendingPanels(), [&](const auto &entry) {
      return std::string(entry.desc.name) == name;
    });
    PanelManager::remove(name.c_str());
  });
  pendingPanels().push_back(std::move(registration));
  return true;
}

std::vector<std::unique_ptr<HudPanel>> takePanels() {
  // The descriptors stay put: onLoad runs once per process but the panel
  // list is rebuilt after every HUD_Shutdown, so each install cycle gets
  // fresh wrappers over the same module-owned descriptor (its strings live
  // in the module image, which outlives every HUD cycle).
  std::vector<std::unique_ptr<HudPanel>> panels;
  panels.reserve(pendingPanels().size());
  for (const auto &entry : pendingPanels()) {
    if (module_resources::enabled(entry.lifetime.owner()))
      panels.push_back(
          std::make_unique<ModulePanel>(entry.desc, entry.lifetime.owner()));
  }
  return panels;
}

} // namespace modmanager
} // namespace framework
