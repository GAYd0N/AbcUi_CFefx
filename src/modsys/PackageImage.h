#pragma once

// framework::PackageImage - a module package file mapped read-only.
//
// The zip stays on disk (`svencoop/abc/abcui/modules/<file>.zip`, never
// inside a VPK) and is mapped with CreateFileMapping/MapViewOfFile, so every
// STORED entry read is a view into the file cache: no staging copy of the
// archive or its entries. The image owns the OS mapping; PackageFile parses
// it in place.

#include <cstddef>
#include <memory>
#include <string>

#include "PackageFile.h"

namespace framework {

class PackageImage {
public:
  // Maps `path` (UTF-8) and parses its central directory. Null with `error`
  // set when the file cannot be opened, mapped or parsed.
  [[nodiscard]] static std::unique_ptr<PackageImage> open(const char *path,
                                                          std::string *error);
  ~PackageImage();

  PackageImage(const PackageImage &) = delete;
  PackageImage &operator=(const PackageImage &) = delete;

  const void *base() const {
    return View;
  }
  size_t size() const {
    return Size;
  }
  PackageFile &file() {
    return *File;
  }

private:
  PackageImage() = default;

  void *FileHandle = nullptr; // CreateFileW
  void *Mapping = nullptr;    // CreateFileMapping
  void *View = nullptr;       // MapViewOfFile
  size_t Size = 0;
  std::unique_ptr<PackageFile> File;
};

} // namespace framework
