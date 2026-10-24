// framework::PackageFile implementation - see PackageFile.h.
#include "PackageFile.h"

#include <cstring>

namespace framework {
namespace {

constexpr std::uint32_t kLocalHeaderSignature = 0x04034b50u;
constexpr size_t kLocalHeaderSize = 30;
constexpr size_t kMinPackageSize = 22; // empty-archive EOCD
constexpr std::uint16_t kDataDescriptorBit = 0x0008;

void fail(std::string *error, const char *message) {
  if (error)
    *error = message;
}

std::uint16_t readLE16(const std::byte *p) {
  return static_cast<std::uint16_t>(std::to_integer<unsigned>(p[0]) |
                                    (std::to_integer<unsigned>(p[1]) << 8));
}

std::uint32_t readLE32(const std::byte *p) {
  return static_cast<std::uint32_t>(std::to_integer<unsigned>(p[0])) |
         (static_cast<std::uint32_t>(std::to_integer<unsigned>(p[1])) << 8) |
         (static_cast<std::uint32_t>(std::to_integer<unsigned>(p[2])) << 16) |
         (static_cast<std::uint32_t>(std::to_integer<unsigned>(p[3])) << 24);
}

} // namespace

std::unique_ptr<PackageFile> PackageFile::open(const void *base, size_t size,
                                               std::string *error) {
  if (!base || size < kMinPackageSize) {
    fail(error, "package image is too small to be a zip archive");
    return nullptr;
  }
  std::unique_ptr<PackageFile> package(new PackageFile());
  mz_zip_zero_struct(&package->Archive);
  if (!mz_zip_reader_init_mem(&package->Archive, base, size, 0)) {
    fail(error, "package image is not a readable zip archive");
    return nullptr;
  }
  package->Base = static_cast<const std::byte *>(base);
  package->BaseSize = size;
  return package;
}

PackageFile::~PackageFile() {
  mz_zip_reader_end(&Archive);
}

size_t PackageFile::entryCount() const {
  return mz_zip_reader_get_num_files(&Archive);
}

std::string PackageFile::entryName(size_t index) const {
  if (index >= entryCount())
    return {};
  mz_zip_archive_file_stat stat;
  if (!mz_zip_reader_file_stat(&Archive, static_cast<mz_uint>(index), &stat))
    return {};
  return stat.m_filename;
}

bool PackageFile::exists(const char *name) const {
  if (!name)
    return false;
  return mz_zip_reader_locate_file(&Archive, name, nullptr,
                                   MZ_ZIP_FLAG_CASE_SENSITIVE) >= 0;
}

bool PackageFile::read(const char *name, PackageEntry &out,
                       std::string *error) const {
  if (!name) {
    fail(error, "package entry name is null");
    return false;
  }
  const int index = mz_zip_reader_locate_file(&Archive, name, nullptr,
                                              MZ_ZIP_FLAG_CASE_SENSITIVE);
  if (index < 0) {
    fail(error, "package entry not found");
    return false;
  }
  mz_zip_archive_file_stat stat;
  if (!mz_zip_reader_file_stat(&Archive, static_cast<mz_uint>(index), &stat)) {
    fail(error, "package entry is unreadable");
    return false;
  }

  if (stat.m_uncomp_size > 128u * 1024 * 1024) {
    fail(error, "package entry exceeds 128 MiB");
    return false;
  }
  const bool hasDescriptor = (stat.m_bit_flag & kDataDescriptorBit) != 0;
  if (stat.m_method == MZ_NO_COMPRESSION && !hasDescriptor) {
    // Zero-copy: the entry bytes sit in the mapped image behind the local
    // header; the name and extra field lengths in the local header decide
    // where the payload starts (they may differ from the central dir copy).
    const size_t headerOfs = static_cast<size_t>(stat.m_local_header_ofs);
    if (headerOfs > BaseSize || headerOfs + kLocalHeaderSize > BaseSize) {
      fail(error, "package entry local header is out of bounds");
      return false;
    }
    const std::byte *header = Base + headerOfs;
    if (readLE32(header) != kLocalHeaderSignature) {
      fail(error, "package entry local header signature mismatch");
      return false;
    }
    const size_t dataOfs = headerOfs + kLocalHeaderSize +
                           readLE16(header + 26) + readLE16(header + 28);
    if (dataOfs > BaseSize || stat.m_comp_size > BaseSize - dataOfs) {
      fail(error, "package entry data is out of bounds");
      return false;
    }
    out.owned.reset();
    out.data = Base + dataOfs;
    out.size = static_cast<size_t>(stat.m_uncomp_size);
    return true;
  }

  size_t extractedSize = 0;
  void *extracted = mz_zip_reader_extract_to_heap(
      &Archive, static_cast<mz_uint>(index), &extractedSize, 0);
  if (!extracted) {
    fail(error, "package entry extraction failed");
    return false;
  }
  out.owned.reset(static_cast<std::byte *>(extracted));
  out.data = out.owned.get();
  out.size = extractedSize;
  return true;
}

} // namespace framework
