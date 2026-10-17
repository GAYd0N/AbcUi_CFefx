#pragma once

// framework::ResourceFile - read-only access to engine resources.
//
// Resource paths are engine-relative and resolve through the Metahook
// IFileSystem (mh_interface_t::FileSystem). VGUI2Extension binds that same
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
bool normalize(std::string_view path, std::string& out);

// Engine filesystem lookup across every mounted search path.
bool exists(std::string_view path);

// Reads the complete resource. Fails when the engine filesystem is not ready,
// the path is invalid, or the file is larger than maxBytes. `bytes` is empty
// on failure (and for a valid zero-byte resource).
bool read(std::string_view path, std::vector<unsigned char>& bytes, std::size_t maxBytes);

}

}
