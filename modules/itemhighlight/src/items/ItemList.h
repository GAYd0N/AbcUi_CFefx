#pragma once

// itemlist - the item-highlight wire format: the package's
// hud/ItemHighLightList.txt is a whitespace-token stream of six-field records
// (model path, localization token, R, G, B, A). Parsing is pure string and
// number work; the module keeps the engine-facing half (vfs read, model index
// resolution, logging) and only receives the records.

#include <string>
#include <vector>

namespace itemlist {

struct Entry {
  std::string path;
  std::string nameToken;
  unsigned char color[4] = {255, 255, 255, 255};
};

// Appends one Entry per complete record. Returns false when the stream ends
// with a partial record (the caller reports it; the fragment is dropped).
bool parse(const std::string &content, std::vector<Entry> &out);

} // namespace itemlist
