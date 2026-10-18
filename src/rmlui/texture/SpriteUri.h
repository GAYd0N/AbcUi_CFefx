#pragma once

// data::sprite URI parsing - the crop forms shared by the HUD sprite cache.
// Kept engine-free so the path/rect split can be unit-tested: the atlas is a
// resource path (server-provided weapon sets live under subdirectories), so
// the crop separator is the last '/' before four integers, not the first one.

#include <cstddef>
#include <string>

namespace sprites {

// Splits "<scheme>://<atlas>/<l>,<t>,<r>,<b>" where bodyStart is the index of
// the atlas (the length of the scheme prefix). Fills atlasFile and rect in
// left/top/right/bottom order. False when no slash is followed by four
// comma-separated integers or the atlas is empty.
bool splitCropUri(const std::string &uri, std::size_t bodyStart,
                  std::string &atlasFile, int rect[4]);

} // namespace sprites
