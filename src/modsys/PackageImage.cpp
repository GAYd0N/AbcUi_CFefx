// framework::PackageImage implementation - see PackageImage.h.
#include "PackageImage.h"

#include <string>

#include <Windows.h>

namespace framework {
namespace {

std::wstring wideFromUtf8(const std::string &text) {
  const int length = MultiByteToWideChar(
      CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(static_cast<size_t>(length > 0 ? length : 0), L'\0');
  if (length > 0) {
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        wide.data(), length);
  }
  return wide;
}

} // namespace

std::unique_ptr<PackageImage> PackageImage::open(const char *path,
                                                 std::string *error) {
  if (!path || !*path) {
    if (error)
      *error = "package path is empty";
    return nullptr;
  }
  const std::wstring wide = wideFromUtf8(path);
  std::unique_ptr<PackageImage> image(new PackageImage());

  image->FileHandle = CreateFileW(
      wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (image->FileHandle == INVALID_HANDLE_VALUE) {
    if (error)
      *error = "package file cannot be opened";
    return nullptr;
  }

  LARGE_INTEGER fileSize{};
  if (!GetFileSizeEx(image->FileHandle, &fileSize) || fileSize.QuadPart <= 0 ||
      fileSize.QuadPart > 1024ll * 1024 * 1024) {
    if (error)
      *error = "package file size is unreadable or empty";
    return nullptr;
  }

  image->Mapping = CreateFileMappingW(image->FileHandle, nullptr, PAGE_READONLY,
                                      0, 0, nullptr);
  if (!image->Mapping) {
    if (error)
      *error = "package file cannot be mapped";
    return nullptr;
  }

  image->View = MapViewOfFile(image->Mapping, FILE_MAP_READ, 0, 0, 0);
  if (!image->View) {
    if (error)
      *error = "package mapping view failed";
    return nullptr;
  }

  image->Size = static_cast<size_t>(fileSize.QuadPart);

  image->File = PackageFile::open(image->View, image->Size, error);
  if (!image->File) {
    // `error` already carries the parse failure.
    return nullptr;
  }
  return image;
}

PackageImage::~PackageImage() {
  File.reset();
  if (View)
    UnmapViewOfFile(View);
  if (Mapping)
    CloseHandle(Mapping);
  if (FileHandle && FileHandle != INVALID_HANDLE_VALUE)
    CloseHandle(FileHandle);
}

} // namespace framework
