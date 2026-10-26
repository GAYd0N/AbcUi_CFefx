// framework::ModuleVfs implementation - see ModuleVfs.h.
#include "ModuleVfs.h"

#include <map>
#include <algorithm>
#include <vector>

namespace framework {
namespace ModuleVfs {
namespace {

constexpr std::string_view kScheme = "mod://";

struct Mount {
  std::shared_ptr<PackageImage> image;
  std::vector<std::string> dependencies;
};

std::map<std::string, Mount> &mounts() {
  static std::map<std::string, Mount> instance;
  return instance;
}

// Canonical package path: '/' separators, no '.', '..' or repeated
// separators. Returns false for paths escaping the root.
bool normalizePath(std::string_view path, std::string &out) {
  if (path.empty() || path.front() == '/' ||
      path.find_first_of("\\:\0?#", 0, 5) != std::string_view::npos)
    return false;
  for (const unsigned char c : path)
    if (c < 0x20 || c == 0x7f)
      return false;
  std::vector<std::string_view> segments;
  size_t start = 0;
  while (start <= path.size()) {
    size_t end = path.find('/', start);
    if (end == std::string_view::npos)
      end = path.size();
    const std::string_view segment = path.substr(start, end - start);
    start = end + 1;
    if (segment.empty() || segment == ".")
      continue;
    if (segment == "..") {
      if (segments.empty())
        return false;
      segments.pop_back();
      continue;
    }
    segments.push_back(segment);
  }
  if (segments.empty())
    return false;
  out.clear();
  for (const std::string_view segment : segments) {
    if (!out.empty())
      out.push_back('/');
    out.append(segment);
  }
  return true;
}

} // namespace

bool isModuleUri(std::string_view uri) {
  return uri.size() > kScheme.size() && uri.starts_with(kScheme);
}

bool mount(const char *moduleId, std::unique_ptr<PackageImage> image,
           std::vector<std::string> dependencies) {
  if (!moduleId || !*moduleId || !image)
    return false;
  auto &table = mounts();
  if (table.contains(moduleId))
    return false;
  Mount entry;
  entry.image = std::move(image);
  entry.dependencies = std::move(dependencies);
  table.emplace(moduleId, std::move(entry));
  return true;
}

void unmount(const char *moduleId) {
  if (!moduleId)
    return;
  mounts().erase(moduleId);
}

void unmountAll() {
  mounts().clear();
}

bool isMounted(const char *moduleId) {
  if (!moduleId)
    return false;
  const auto &table = mounts();
  return table.contains(moduleId);
}

std::vector<std::string> mountedIds() {
  std::vector<std::string> ids;
  ids.reserve(mounts().size());
  for (const auto &[id, mount] : mounts())
    ids.push_back(id);
  return ids;
}

bool open(const char *uri, PackageEntry &out, std::string *error) {
  out = {};
  if (!uri || !isModuleUri(uri)) {
    if (error)
      *error = "not a module uri";
    return false;
  }
  const std::string_view rest(uri + kScheme.size());
  const size_t slash = rest.find('/');
  if (slash == std::string_view::npos || slash == 0) {
    if (error)
      *error = "module uri has no path";
    return false;
  }
  const std::string moduleId(rest.substr(0, slash));
  std::string path;
  if (!normalizePath(rest.substr(slash + 1), path)) {
    if (error)
      *error = "module uri path is invalid";
    return false;
  }
  return openPath(moduleId.c_str(), path.c_str(), out, error);
}

bool openPath(const char *moduleId, const char *path, PackageEntry &out,
              std::string *error) {
  out = {};
  if (!moduleId || !path) {
    if (error)
      *error = "module lookup arguments are null";
    return false;
  }
  const auto &table = mounts();
  const auto it = table.find(moduleId);
  if (it == table.end()) {
    if (error)
      *error = "module is not mounted";
    return false;
  }
  std::string normalized;
  if (!normalizePath(path, normalized)) {
    if (error)
      *error = "invalid package path";
    return false;
  }
  if (!it->second.image->file().read(normalized.c_str(), out, error))
    return false;
  out.mappingPin = it->second.image;
  return true;
}

bool resolve(std::string_view base, std::string_view path, std::string &uri) {
  std::string input;
  if (isModuleUri(path)) {
    input = path;
  } else {
    if (!isModuleUri(base))
      return false;
    const auto slash = base.rfind('/');
    if (slash < 6 || slash == base.npos)
      return false;
    input = std::string(base.substr(0, slash + 1)) + std::string(path);
  }
  const auto slash = input.find('/', 6);
  if (slash == input.npos || slash == 6)
    return false;
  const auto id = std::string_view(input).substr(6, slash - 6);
  if (id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.") != id.npos)
    return false;
  std::string normalized;
  if (!normalizePath(std::string_view(input).substr(slash + 1), normalized))
    return false;
  uri = input.substr(0, slash + 1) + normalized;
  if (isModuleUri(base)) {
    const auto sourceSlash = base.find('/', 6);
    if (sourceSlash == base.npos ||
        !canRead(base.substr(6, sourceSlash - 6), uri))
      return false;
  }
  return true;
}

bool canRead(std::string_view requester, std::string_view uri) {
  std::string canonical;
  if (!resolve({}, uri, canonical))
    return false;
  const auto slash = canonical.find('/', 6);
  const std::string provider = canonical.substr(6, slash - 6);
  if (!isMounted(provider.c_str()))
    return false;
  if (requester == provider)
    return true;
  const auto it = mounts().find(std::string(requester));
  return it != mounts().end() &&
         std::find(it->second.dependencies.begin(),
                   it->second.dependencies.end(),
                   provider) != it->second.dependencies.end();
}

std::vector<std::string> list(const char *directory) {
  std::vector<std::string> result;
  if (!directory)
    return result;
  // A sentinel canonicalizes directory paths without accepting root escapes.
  std::string base = directory;
  if (!base.ends_with('/'))
    base += '/';
  std::string canonical;
  if (!resolve({}, base + "_", canonical))
    return result;
  canonical.pop_back();
  const auto slash = canonical.find('/', 6);
  const auto it = mounts().find(canonical.substr(6, slash - 6));
  if (it == mounts().end())
    return result;
  const auto prefix = canonical.substr(slash + 1);
  const auto &file = it->second.image->file();
  for (size_t i = 0; i < file.entryCount(); ++i) {
    auto name = file.entryName(i);
    if (name.starts_with(prefix) && !name.ends_with('/'))
      result.push_back(canonical.substr(0, slash + 1) + name);
  }
  std::sort(result.begin(), result.end());
  return result;
}

bool readText(const char *uri, std::string &out, size_t maxBytes) {
  out.clear();
  PackageEntry entry;
  if (!open(uri, entry, nullptr) || entry.size > maxBytes)
    return false;
  out.assign(static_cast<const char *>(entry.data), entry.size);
  if (out.starts_with("\xef\xbb\xbf"))
    out.erase(0, 3);
  return true;
}

} // namespace ModuleVfs
} // namespace framework
