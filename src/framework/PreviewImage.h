#pragma once
#include "ImageLoader.h"
#include <string>
#include <vector>

namespace framework {
// URI validation and preview color policy only. ImageLoader/FreeImage owns
// image decoding and palette access; the renderer owns mounted IO and textures.
struct PreviewRequest {
  std::string Path;
  bool Model = false;
  int Top = 0;
  int Bottom = 0;
  std::string SprayColor;
};
bool parsePreviewUri(const std::string &Uri, PreviewRequest &Output);
bool decodePreview(const PreviewRequest &Request,
                   const std::vector<unsigned char> &Bytes,
                   ImageLoader::DecodedImage &Output);
} // namespace framework
