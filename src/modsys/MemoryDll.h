#pragma once

// framework::MemoryDll - load a PE image from memory without LoadLibrary.
//
// Module entry DLLs arrive as package bytes (a zero-copy view into a mapped
// zip). The image is committed with VirtualAlloc, sections are copied, base
// relocations are applied and the import table resolves through
// LoadLibraryA + GetProcAddress - first-party modules only import the system
// DLLs and the /MD CRT, all of which are real loaded modules. DllMain runs
// with DLL_PROCESS_ATTACH on success; unload runs DllMain(DETACH), releases
// the imported modules and frees the image.
//
// x86 SEH and C++ exceptions work for manually mapped images (the FS-chain
// needs no kernel registration); static TLS, delay-loaded imports and .NET
// images are rejected with a load error instead of failing silently later.
// These constraints are documented in the SDK.

#include <cstddef>
#include <string>
#include <vector>

namespace framework {

class MemoryDll {
public:
  MemoryDll() = default;
  ~MemoryDll();

  MemoryDll(const MemoryDll &) = delete;
  MemoryDll &operator=(const MemoryDll &) = delete;

  // Maps and initializes the image. False with `error` set on any failure.
  // `data` is only read during the call: sections are copied into the
  // committed image.
  bool load(const void *data, size_t size, std::string *error);
  // DllMain(DETACH), import release, image free. Idempotent.
  void unload();

  bool isLoaded() const {
    return Image != nullptr;
  }

  // Resolves an exported symbol by name after load; null when missing or
  // when the export is a forwarded one (first-party modules never forward).
  void *getExport(const char *name) const;

private:
  void *Image = nullptr;       // committed image base (entry point lives here)
  size_t ImageSize = 0;        // SizeOfImage (virtual)
  std::vector<void *> Imports; // modules to FreeLibrary at unload
};

} // namespace framework
