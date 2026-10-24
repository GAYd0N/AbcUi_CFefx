#pragma once

// Package-owned UTF-8 translation tables. Explicit <module-id>/<token>
// namespaces; unqualified SDK lookups use the current module's namespace.
// Each module loads English first, then its active-language overrides.

#include <string>

namespace framework {

class Localization {
public:
  // Reloads the mounted packages for the detected language.
  static void load();

  // True and fills `out` when the token is known; leaves `out` alone and
  // returns false otherwise.
  static bool translate(std::string &out, const std::string &token);

private:
  Localization() = default;
};

} // namespace framework
