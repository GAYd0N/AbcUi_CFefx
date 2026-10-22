#pragma once

// framework::ModuleManifest - the parsed manifest.json of a module package.
//
// Required fields: id, name, description, author, version, abi, minAbcUi,
// entry. Optional arrays: dependencies, conflicts (module ids). Parsing is
// strict: an unknown shape fails with a field-level error message, because a
// half-understood manifest would otherwise load a module that then breaks in
// subtler ways.

#include <string>
#include <vector>

namespace framework {

struct ModuleManifest {
  std::string id;
  std::string name;
  std::string description;
  std::string author;
  std::string version;
  int abi = 0;
  std::string minAbcUi;
  std::string entry; // package-internal path of the entry DLL
  std::vector<std::string> dependencies;
  std::vector<std::string> conflicts;
};

// Parses UTF-8 manifest text. False with `error` set on any structural or
// type failure.
bool parseManifest(const char *text, size_t size, ModuleManifest &out,
                   std::string *error);

// Compares dotted numeric versions ("0.8.1" vs "0.8"): negative when `left`
// is older, zero when equal, positive when newer. Missing segments are zero.
int compareVersions(const std::string &left, const std::string &right);

} // namespace framework
