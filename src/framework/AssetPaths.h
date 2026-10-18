#pragma once

// framework::assets - asset-tree path migrations.
//
// The GameUI/hud split moved the menu media from abc/abcui/scence to
// abc/abcui/gameui/scence. Both media cvars are FCVAR_ARCHIVE, so an
// existing config.cfg may still hold the old path. Remap it on read rather
// than silently losing the player's chosen movie or track.

#include <string>

namespace framework::assets {

inline std::string remapLegacyMenuMedia(const std::string &path) {
  constexpr const char *kLegacyPrefix = "abc/abcui/scence/";
  constexpr const char *kCurrentPrefix = "abc/abcui/gameui/scence/";
  if (path.rfind(kLegacyPrefix, 0) != 0)
    return path;
  return std::string(kCurrentPrefix) +
         path.substr(std::char_traits<char>::length(kLegacyPrefix));
}

} // namespace framework::assets
