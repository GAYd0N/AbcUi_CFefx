#pragma once

// framework::ImageLoader - FreeImage-backed multi-format image decoding
// (PNG/JPG/TGA/BMP/... and everything else the pinned FreeImage_clone reads).
// Feeds every AbcUi texture source that starts as an encoded image file or
// buffer (plan 05 §3.2 LoadTexture path).
//
// Seam rules: decode only - no Rml types, no VGUI2 surface, no renderer
// calls. Consumers upload DecodedImage through their own texture channel
// (rmlui::AbcRenderInterface for RML-referenced files, data::avatars for the
// default avatar). The single FreeImage dependency of the plugin lives here;
// callers never include FreeImage.h.

#include <cstddef>
#include <functional>
#include <vector>

namespace framework {

namespace ImageLoader {

// Decoded pixels: top-down RGBA8, row-major, Width * Height * 4 bytes.
// FreeImage's bottom-up BGRA scan order is normalized away here so every
// consumer sees the same layout (matching Rml's (0,0)=top-left UV convention).
struct DecodedImage {
  std::vector<unsigned char> Pixels;
  int Width = 0;
  int Height = 0;

  bool valid() const {
    return Width > 0 && Height > 0 && !Pixels.empty();
  }
};

// Optional indexed-image processing, after FreeImage has decoded the image
// and before conversion to RGBA. Never exposes bitmap headers, palette byte
// layouts or FreeImage objects to callers. Truecolor images are unaffected.
struct PaletteColor {
  unsigned char Red = 0;
  unsigned char Green = 0;
  unsigned char Blue = 0;
};
using PaletteTransform =
    std::function<void(unsigned Index, PaletteColor &Color)>;

// An 8-bit indexed image: one palette index per pixel (top-down, row-major)
// plus the palette the indices refer to. The spray encoder's input format.
struct IndexedImage {
  std::vector<unsigned char> Indices;
  std::vector<PaletteColor> Palette;
  int Width = 0;
  int Height = 0;

  bool valid() const {
    return Width > 0 && Height > 0 && !Palette.empty() &&
           Indices.size() == static_cast<std::size_t>(Width) *
                                 static_cast<std::size_t>(Height);
  }
};

// Reads an engine-relative resource through the mounted IFileSystem search
// paths, detects the format from the file content (file signature, with the
// name only as fallback for headerless formats) and decodes it. Returns false
// silently on any failure (unknown format, unreadable, bad header) - the
// asset verification layer owns missing-file warnings, LoadTexture retries
// are Rml's business.
bool loadFromFile(const char *path, DecodedImage &out);

// Same contract for an in-memory buffer (data already pulled through another
// channel). Format detection runs on the leading bytes.
bool loadFromMemory(const unsigned char *Data, std::size_t Size,
                    DecodedImage &Output,
                    const PaletteTransform &Transform = {});

// Bilinear resize to exactly Width x Height (FreeImage's default filter, the
// same one the original spray path rescaled with).
bool rescale(const DecodedImage &Input, int Width, int Height,
             DecodedImage &Output);

// Wu quantization to at most MaxColors (1..256) entries. The returned palette
// holds exactly the entries the indices use.
bool quantizeIndexed(const DecodedImage &Input, int MaxColors,
                     IndexedImage &Output);

} // namespace ImageLoader

} // namespace framework
