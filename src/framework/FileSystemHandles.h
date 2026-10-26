#pragma once

#include <IFileSystem.h>

#include <memory>

namespace framework {

// Keep the creating filesystem with the handle, not a mutable global lookup.
struct EngineFileCloser {
  IFileSystem *filesystem = nullptr;

  void operator()(void *file) const noexcept {
    filesystem->Close(file);
  }
};

using EngineFile = std::unique_ptr<void, EngineFileCloser>;

inline EngineFile openEngineFile(IFileSystem &filesystem, const char *path,
                                 const char *mode) {
  return EngineFile(filesystem.Open(path, mode), {&filesystem});
}

class EngineFileSearch {
public:
  explicit EngineFileSearch(IFileSystem &filesystem)
      : filesystem_(filesystem) {}
  ~EngineFileSearch() {
    if (handle != FILESYSTEM_INVALID_FIND_HANDLE)
      filesystem_.FindClose(handle);
  }

  EngineFileSearch(const EngineFileSearch &) = delete;
  EngineFileSearch &operator=(const EngineFileSearch &) = delete;

  FileFindHandle_t handle = FILESYSTEM_INVALID_FIND_HANDLE;

private:
  IFileSystem &filesystem_;
};

} // namespace framework
