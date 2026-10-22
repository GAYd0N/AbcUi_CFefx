#include "SettingsFileBrowser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace settingspage::files {
namespace {

// A folder with more entries than this shows the first window and says so;
// listing stays bounded whatever the user opens.
constexpr std::size_t kMaxEntries = 512;

std::string toUtf8(const std::filesystem::path &path) {
  const auto text = path.u8string();
  return std::string(text.begin(), text.end());
}

std::string lowerExtension(const std::string &name) {
  const auto dot = name.find_last_of('.');
  if (dot == std::string::npos)
    return {};
  std::string extension = name.substr(dot);
  std::transform(
      extension.begin(), extension.end(), extension.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension;
}

bool existsDirectory(const std::string &path) {
  std::error_code error;
  return std::filesystem::is_directory(path, error);
}

// Namespace scope, not a function-local static: the module image is mapped
// without static TLS support and MSVC's local-static guard needs a TLS slot.
const std::vector<std::string> ImageExtensions = {
    ".png", ".jpg", ".jpeg", ".bmp",  ".tga",
    ".gif", ".pcx", ".tif",  ".tiff", ".webp"};

} // namespace

const std::vector<std::string> &FileBrowser::imageExtensions() {
  return ImageExtensions;
}

bool FileBrowser::isImageName(const std::string &name) {
  const std::string extension = lowerExtension(name);
  if (extension.empty())
    return false;
  const auto &all = imageExtensions();
  return std::find(all.begin(), all.end(), extension) != all.end();
}

std::vector<Root> FileBrowser::defaultRoots() {
  std::vector<Root> roots;
  std::error_code error;
  const auto working = std::filesystem::current_path(error);
  if (!error)
    roots.push_back({"Game", toUtf8(working)});
  if (const char *profile = std::getenv("USERPROFILE")) {
    for (const char *name : {"Pictures", "Desktop"}) {
      const std::string candidate = std::string(profile) + "\\" + name;
      if (existsDirectory(candidate))
        roots.push_back({name, candidate});
    }
  }
  for (char drive = 'A'; drive <= 'Z'; ++drive) {
    const std::string path = std::string(1, drive) + ":\\";
    std::error_code driveError;
    if (!std::filesystem::exists(path, driveError) || driveError)
      continue;
    roots.push_back({path, path});
  }
  return roots;
}

bool FileBrowser::open(const std::string &directory) {
  if (!existsDirectory(directory)) {
    Error = "not a directory: " + directory;
    return false;
  }
  return list(directory);
}

bool FileBrowser::enter(const std::string &name) {
  if (Directory.empty())
    return false;
  std::error_code error;
  const auto child = std::filesystem::path(Directory) / name;
  if (!std::filesystem::is_directory(child, error) || error)
    return false;
  return list(toUtf8(std::filesystem::weakly_canonical(child, error)));
}

bool FileBrowser::openParent() {
  if (Directory.empty())
    return false;
  std::error_code error;
  const auto parent = std::filesystem::path(Directory).parent_path();
  if (parent.empty()) {
    Error = "already at the top";
    return false;
  }
  return list(toUtf8(parent));
}

bool FileBrowser::list(const std::string &directory) {
  std::error_code error;
  std::vector<Entry> found;
  std::filesystem::directory_iterator iterator(directory, error);
  if (error) {
    Error = "cannot open: " + directory;
    return false;
  }
  for (const auto &item : iterator) {
    if (found.size() >= kMaxEntries)
      break;
    const std::string name = toUtf8(item.path().filename());
    if (name.empty() || name[0] == '.')
      continue;
    std::error_code entryError;
    const bool isDirectory = item.is_directory(entryError);
    if (entryError)
      continue;
    if (!isDirectory && !isImageName(name))
      continue;
    found.push_back({name, isDirectory});
  }
  // Directories first, then files, each alphabetically (case-insensitive).
  std::sort(found.begin(), found.end(), [](const Entry &a, const Entry &b) {
    if (a.Directory != b.Directory)
      return a.Directory;
    std::string left = a.Name;
    std::string right = b.Name;
    std::transform(left.begin(), left.end(), left.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    std::transform(
        right.begin(), right.end(), right.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return left < right;
  });
  Directory = directory;
  Entries = std::move(found);
  Error.clear();
  return true;
}

} // namespace settingspage::files
