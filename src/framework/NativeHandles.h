#pragma once

#include <windows.h>

#include <memory>
#include <type_traits>

namespace framework {

struct NativeFindCloser {
  void operator()(void *handle) const noexcept {
    if (handle != INVALID_HANDLE_VALUE)
      FindClose(handle);
  }
};
using NativeFindHandle = std::unique_ptr<void, NativeFindCloser>;

struct NativeLibraryCloser {
  void operator()(HMODULE module) const noexcept {
    FreeLibrary(module);
  }
};
using NativeLibrary =
    std::unique_ptr<std::remove_pointer_t<HMODULE>, NativeLibraryCloser>;

} // namespace framework
