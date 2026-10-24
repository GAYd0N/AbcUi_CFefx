// framework::ResourceFile implementation - see ResourceFile.h.
#include "ResourceFile.h"
#include "modsys/ModuleVfs.h"
#include <cstring>

#include <abc/Plugin.h>

#include <IFileSystem.h>

#include <limits>

namespace framework {

namespace ResourceFile {

bool normalize(std::string_view path, std::string &out) {
  out.clear();
  if (path.empty() || path.front() == '/' || path.front() == '\\')
    return false;
  if (path.size() >= 2 && path[1] == ':')
    return false;

  std::vector<std::string_view> components;
  for (size_t begin = 0; begin <= path.size();) {
    size_t end = path.find_first_of("/\\", begin);
    if (end == std::string_view::npos)
      end = path.size();
    const std::string_view component = path.substr(begin, end - begin);
    if (component.empty() || component == ".") {
      // Collapse repeated separators and current-directory components.
    } else if (component == "..") {
      if (components.empty())
        return false;
      components.pop_back();
    } else {
      components.push_back(component);
    }
    if (end == path.size())
      break;
    begin = end + 1;
  }
  if (components.empty())
    return false;

  for (size_t i = 0; i < components.size(); ++i) {
    if (i)
      out.push_back('/');
    out.append(components[i]);
  }
  return true;
}

bool exists(std::string_view path) {
  if (path.starts_with("abc/abcui/"))
    return false;
  if (ModuleVfs::isModuleUri(path)) {
    PackageEntry entry;
    return ModuleVfs::open(std::string(path).c_str(), entry, nullptr);
  }
  std::string normalized;
  if (!abc::g_pFileSystem || !normalize(path, normalized) ||
      normalized.starts_with("abc/abcui/"))
    return false;
  return abc::g_pFileSystem->FileExists(normalized.c_str());
}

bool read(std::string_view path, std::vector<std::byte> &bytes,
          std::size_t maxBytes) {
  bytes.clear();
  if (path.starts_with("abc/abcui/"))
    return false;
  if (ModuleVfs::isModuleUri(path)) {
    PackageEntry entry;
    if (!ModuleVfs::open(std::string(path).c_str(), entry, nullptr) ||
        entry.size > maxBytes)
      return false;
    bytes.resize(entry.size);
    if (entry.size)
      std::memcpy(bytes.data(), entry.data, entry.size);
    return true;
  }
  std::string normalized;
  if (!abc::g_pFileSystem || maxBytes == 0 || !normalize(path, normalized) ||
      normalized.starts_with("abc/abcui/"))
    return false;

  FileHandle_t file = abc::g_pFileSystem->Open(normalized.c_str(), "rb");
  if (!file)
    return false;

  struct CloseFile {
    FileHandle_t Handle;
    ~CloseFile() {
      if (Handle)
        abc::g_pFileSystem->Close(Handle);
    }
  } close{file};

  const unsigned size = abc::g_pFileSystem->Size(file);
  if (size > maxBytes ||
      size > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    return false;
  if (size == 0)
    return abc::g_pFileSystem->IsOk(file);

  bytes.resize(size);
  std::size_t total = 0;
  while (total < bytes.size()) {
    const int remaining = static_cast<int>(bytes.size() - total);
    const int got =
        abc::g_pFileSystem->Read(bytes.data() + total, remaining, file);
    if (got <= 0)
      break;
    total += static_cast<std::size_t>(got);
  }
  const bool ok = total == bytes.size() && abc::g_pFileSystem->IsOk(file);
  if (!ok)
    bytes.clear();
  return ok;
}

} // namespace ResourceFile

} // namespace framework
