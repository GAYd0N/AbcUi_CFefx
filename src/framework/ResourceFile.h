#pragma once

// framework::ResourceFile - read-only module and native engine content.
//
// mod:// paths resolve only in their package. Native relative paths use the
// Metahook IFileSystem. VGUI2Extension binds that same
// instance as g_pFullFileSystem, so this is the engine's mounted search path
// view rather than the process CWD: GAME, GAME_DOWNLOADS, GAME_ADDON and the
// other Sven Co-op mounts are all visible here.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace framework {

namespace ResourceFile {

// Canonical engine-relative path: '/' separators, no '.', '..' or repeated
// separators. Returns false for absolute paths and paths escaping the root.
[[nodiscard]] bool normalize(std::string_view path, std::string &out);

// Engine filesystem lookup across every mounted search path.
[[nodiscard]] bool exists(std::string_view path);

// Reads the complete resource. Fails when the engine filesystem is not ready,
// the path is invalid, or the file is larger than maxBytes. `bytes` is empty
// on failure (and for a valid zero-byte resource).
[[nodiscard]] bool read(std::string_view path, std::vector<std::byte> &bytes,
                        std::size_t maxBytes);

} // namespace ResourceFile

} // namespace framework
