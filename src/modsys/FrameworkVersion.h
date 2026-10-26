#pragma once

// framework::kFrameworkVersion - single source of truth for the plugin
// version. CMake scrapes the literal for dist naming and the module loader
// compares it against manifest minAbcUi requirements, so both stay one edit
// from the plugin ctor that reports it.

namespace framework {

inline constexpr const char *kFrameworkVersion = "0.9.2";

} // namespace framework
