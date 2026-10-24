#pragma once

// framework::PackageFile - read-only zip package indexed in place.
//
// A package is a plain zip whose entries are addressed by '/'. The archive is
// parsed with miniz over a caller-owned memory image (PackageImage owns the
// memory-mapped file), so no copy of the archive is taken. STORED entries
// without a data descriptor are served as zero-copy views straight into the
// image; everything else (DEFLATED, or STORED with a trailing descriptor) is
// extracted once into an owned buffer. First-party packages written by
// tools/pack.py always store entries uncompressed, which keeps every hot read
// (manifest, rml, rcss, the entry DLL) zero-copy.
//
// Lookups are exact and case sensitive. Names are never used to extract to
// disk, so path shapes inside the archive are not a traversal concern.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <miniz.h>

namespace framework {

// One opened entry. `data` points into the mapped package for zero-copy
// entries and into `owned` for extracted ones; `owned` must outlive the use
// of the view.
struct PackageEntry {
  const void *data = nullptr;
  size_t size = 0;
  std::unique_ptr<std::byte[], void (*)(void *)> owned{nullptr, mz_free};
  // VFS views retain the backing mapping even if its provider is retired.
  std::shared_ptr<const void> mappingPin;
};

class PackageFile {
public:
  // Parses the central directory of `base`; null with `error` set on failure.
  [[nodiscard]] static std::unique_ptr<PackageFile>
  open(const void *base, size_t size, std::string *error);
  ~PackageFile();

  PackageFile(const PackageFile &) = delete;
  PackageFile &operator=(const PackageFile &) = delete;

  size_t entryCount() const;
  // Entry name at `index`, or an empty string when out of range.
  std::string entryName(size_t index) const;
  bool exists(const char *name) const;
  // Opens an entry; false with `error` set when missing or unreadable.
  bool read(const char *name, PackageEntry &out, std::string *error) const;

private:
  PackageFile() = default;

  // miniz's reader API takes a mutable archive handle even for read-side
  // queries, so the handle is mutable to keep the PackageFile surface const.
  mutable mz_zip_archive Archive;
  const std::byte *Base = nullptr;
  size_t BaseSize = 0;
};

} // namespace framework
