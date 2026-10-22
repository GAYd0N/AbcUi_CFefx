// framework::ModuleVfs implementation - see ModuleVfs.h.
#include "ModuleVfs.h"

#include <map>
#include <vector>

namespace framework {
namespace ModuleVfs {
namespace {

constexpr std::string_view kScheme = "mod://";

struct Mount {
  std::unique_ptr<PackageImage> image;
};

std::map<std::string, Mount> &mounts() {
  static std::map<std::string, Mount> instance;
  return instance;
}

// Canonical package path: '/' separators, no '.', '..' or repeated
// separators. Returns false for paths escaping the root.
bool normalizePath(std::string_view path, std::string &out) {
  if (path.empty())
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
  return uri.size() > kScheme.size() &&
         uri.compare(0, kScheme.size(), kScheme) == 0;
}

bool mount(const char *moduleId, std::unique_ptr<PackageImage> image) {
  if (!moduleId || !*moduleId || !image)
    return false;
  auto &table = mounts();
  if (table.find(moduleId) != table.end())
    return false;
  Mount entry;
  entry.image = std::move(image);
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
  return table.find(moduleId) != table.end();
}

bool open(const char *uri, PackageEntry &out, std::string *error) {
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
  return it->second.image->file().read(path, out, error);
}

} // namespace ModuleVfs
} // namespace framework
