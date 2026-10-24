#pragma once

// settingspage::files - the directory model behind the settings spray picker:
// an absolute directory, its visible children (subdirectories and image
// files), quick roots, and the navigation commands. Pure filesystem reads
// through std::filesystem - no DOM, no engine, no renderer.
//
// Moved into the page module with the page: the picker is page state (the
// framework only receives the confirmed absolute path and runs the maker).

#include <string>
#include <vector>

namespace settingspage::files {

struct Entry {
  std::string Name;
  bool Directory = false;
};

// A quick jump offered at the top of the picker (game folder, Pictures, ...).
struct Root {
  std::string Label;
  std::string Path;
};

class FileBrowser {
public:
  // Extensions the picker offers (FreeImage reads all of them).
  static const std::vector<std::string> &imageExtensions();
  // True when the file name carries one of the image extensions.
  static bool isImageName(const std::string &name);
  // The roots the picker starts from: the process working directory (the
  // game folder the engine launched from), %USERPROFILE%/Pictures and
  // %USERPROFILE%/Desktop when they exist, then every drive.
  static std::vector<Root> defaultRoots();

  void setRoots(std::vector<Root> value) {
    Roots = std::move(value);
  }
  const std::vector<Root> &roots() const {
    return Roots;
  }

  // Opens an absolute directory and lists it. False keeps the previous
  // directory and sets error().
  bool open(const std::string &directory);
  // Enters a child directory by entry name; false when it is not one.
  bool enter(const std::string &name);
  // Moves to the parent directory (no-op at a drive root).
  bool openParent();

  const std::string &directory() const {
    return Directory;
  }
  const std::vector<Entry> &entries() const {
    return Entries;
  }
  const std::string &error() const {
    return Error;
  }

private:
  bool list(const std::string &directory);

  std::vector<Root> Roots;
  std::string Directory;
  std::vector<Entry> Entries;
  std::string Error;
};

} // namespace settingspage::files
