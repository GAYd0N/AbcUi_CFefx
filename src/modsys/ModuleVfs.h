#pragma once

// framework::ModuleVfs - mounted module packages under the mod:// scheme.
//
// URIs look like "mod://<module-id>/<path>"; the path is normalized ('.'
// and '..' segments collapsed, '/' separators) and resolved against the
// package the module id maps to. Entries come back as PackageEntry views:
// zero-copy pointers into the mapped zip for STORED content, owned buffers
// for extracted ones. Packages stay mounted for the process lifetime - the
// ModuleManager unmounts in reverse load order only after Rml has shut
// down - so zero-copy views remain valid for the whole UI lifetime.
//
// Lookups are case sensitive and must match the archive's entry names.

#include "PackageFile.h"
#include "PackageImage.h"

#include <memory>
#include <string>
#include <string_view>

namespace framework {

namespace ModuleVfs {

// True when the uri carries the mod:// scheme.
bool isModuleUri(std::string_view uri);

// Mounts a parsed package under a module id and takes ownership of the
// image. Fails when the id is empty or already mounted.
bool mount(const char *moduleId, std::unique_ptr<PackageImage> image);
// Drops one mount (load failures) or every mount (process teardown).
void unmount(const char *moduleId);
void unmountAll();

bool isMounted(const char *moduleId);

// Resolves "mod://<id>/<path>" into an entry view. The path may carry '.'
// and '..' segments; they are collapsed before lookup.
bool open(const char *uri, PackageEntry &out, std::string *error);
// Same, for a path inside a known module.
bool openPath(const char *moduleId, const char *path, PackageEntry &out,
              std::string *error);

} // namespace ModuleVfs

} // namespace framework
