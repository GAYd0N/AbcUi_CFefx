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
// The image is not registered with the OS loader; cross-image C++ unwinding
// is not guaranteed. SDK callbacks must not throw across the C ABI; expected
// startup errors use core->failInitialization. Static TLS, delay-loaded
// imports and .NET images are rejected with a load error.

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
  [[nodiscard]] bool load(const void *data, size_t size, std::string *error);
  // Detach only after a successful attach, then release imports and image.
  // Safe after a partial load and idempotent.
  void unload();

  bool isLoaded() const {
    return Image != nullptr;
  }

  // Resolves an exported symbol by name after load; null when missing or
  // when the export is a forwarded one (first-party modules never forward).
  void *getExport(const char *name) const;

private:
  void *Image = nullptr; // committed image base (entry point lives here)
  size_t ImageSize = 0;  // SizeOfImage (virtual)
  void *AttachedEntry = nullptr; // set only after DllMain(ATTACH) succeeds
  std::vector<void *> Imports;   // modules to FreeLibrary at unload
};

} // namespace framework
