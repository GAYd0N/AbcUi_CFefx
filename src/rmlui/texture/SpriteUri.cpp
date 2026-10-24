// data::sprite URI parsing - see SpriteUri.h.
#include "SpriteUri.h"

#include <abc/Parse.h>

#include <string_view>

namespace sprites {

namespace {

// "left,top,right,bottom" - exactly four integers, no trailing text.
bool parseCrop(std::string_view body, int rect[4]) {
  int values[4] = {};
  std::size_t at = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    if (i > 0) {
      if (at >= body.size() || body[at] != ',')
        return false;
      ++at;
    }
    const std::size_t comma = body.find(',', at);
    if (i < 3 && comma == std::string_view::npos)
      return false;
    const std::size_t end = i < 3 ? comma : body.size();
    const auto value = abc::ParseInt(body.substr(at, end - at));
    if (!value)
      return false;
    values[i] = *value;
    at = end + 1;
  }
  for (int i = 0; i < 4; ++i)
    rect[i] = values[i];
  return true;
}

} // namespace

bool splitCropUri(const std::string &uri, std::size_t bodyStart,
                  std::string &atlasFile, int rect[4]) {
  atlasFile.clear();
  std::size_t slash = uri.rfind('/');
  while (slash != std::string::npos && slash >= bodyStart) {
    const std::string_view body(uri.data() + slash + 1, uri.size() - slash - 1);
    if (parseCrop(body, rect)) {
      atlasFile = uri.substr(bodyStart, slash - bodyStart);
      if (atlasFile.empty())
        return false;
      return true;
    }
    if (slash == 0)
      break;
    slash = uri.rfind('/', slash - 1);
  }
  return false;
}

} // namespace sprites
