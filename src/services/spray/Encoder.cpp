#include "services/spray/Encoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace services::spray {
namespace {

constexpr unsigned char kTransparentRed = 0;
constexpr unsigned char kTransparentGreen = 0;
constexpr unsigned char kTransparentBlue = 255;
constexpr unsigned char kAlphaThreshold = 125;
constexpr int kMipLevels = 4;
constexpr int kPaletteSize = 256;
constexpr int kMipTexHeaderSize = 40; // name[16] + width + height + offsets[4]
constexpr int kWadLumpSize = 32;      // the WAD3 lump directory record
constexpr char kLumpName[16] = "{LOGO";

void writeU16(std::vector<unsigned char> &out, unsigned value) {
  out.push_back(static_cast<unsigned char>(value & 0xFF));
  out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
}

void writeU32(std::vector<unsigned char> &out, unsigned value) {
  out.push_back(static_cast<unsigned char>(value & 0xFF));
  out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
  out.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
  out.push_back(static_cast<unsigned char>((value >> 24) & 0xFF));
}

void padTo4(std::vector<unsigned char> &out) {
  while (out.size() % 4 != 0)
    out.push_back(0);
}

} // namespace

bool fitSize(int width, int height, const SizeLimits &limits, int &outWidth,
             int &outHeight) {
  if (width <= 0 || height <= 0)
    return false;
  float widthF = static_cast<float>(width);
  float heightF = static_cast<float>(height);
  if (widthF * heightF > static_cast<float>(limits.MaxArea)) {
    // Long side to MaxSide first, then shrink both until the area fits.
    if (widthF > heightF) {
      heightF = heightF / widthF * static_cast<float>(limits.MaxSide);
      widthF = static_cast<float>(limits.MaxSide);
    } else {
      widthF = widthF / heightF * static_cast<float>(limits.MaxSide);
      heightF = static_cast<float>(limits.MaxSide);
    }
    while (widthF * heightF > static_cast<float>(limits.MaxArea)) {
      widthF *= limits.Shrink;
      heightF *= limits.Shrink;
    }
  }
  int widthOut = static_cast<int>(widthF);
  int heightOut = static_cast<int>(heightF);
  // 16-alignment for every size (not only the capped ones): the four mip
  // levels must divide evenly, which the original only guaranteed by luck
  // for images smaller than the cap.
  const int align = limits.Align;
  widthOut -= widthOut % align;
  heightOut -= heightOut % align;
  if (widthOut < align || heightOut < align)
    return false;
  outWidth = widthOut;
  outHeight = heightOut;
  return true;
}

bool encode(const framework::ImageLoader::DecodedImage &source,
            EncodedSpray &output, std::string &error) {
  output = EncodedSpray();
  error.clear();
  if (!source.valid()) {
    error = "image is empty";
    return false;
  }
  int targetWidth = 0;
  int targetHeight = 0;
  if (!fitSize(source.Width, source.Height, SizeLimits{}, targetWidth,
               targetHeight)) {
    error = "image is too small for a spray";
    return false;
  }
  framework::ImageLoader::DecodedImage sized;
  if (targetWidth == source.Width && targetHeight == source.Height)
    sized = source;
  else if (!framework::ImageLoader::rescale(source, targetWidth, targetHeight,
                                            sized)) {
    error = "resize failed";
    return false;
  }

  // Mask the transparent pixels and force them to the reserved blue so the
  // quantizer keeps a slot close to it (the original's pre-quantize step).
  const int width = sized.Width;
  const int height = sized.Height;
  if (!sized.valid() ||
      sized.Pixels.size() != static_cast<std::size_t>(width) *
                                 static_cast<std::size_t>(height) * 4) {
    error = "unexpected decoded pixel size";
    return false;
  }
  std::vector<unsigned char> mask(static_cast<std::size_t>(width) * height, 0);
  bool hasTransparency = false;
  for (std::size_t i = 0; i < mask.size(); ++i) {
    unsigned char *pixel = sized.Pixels.data() + i * 4;
    if (pixel[3] >= kAlphaThreshold)
      continue;
    mask[i] = 1;
    hasTransparency = true;
    pixel[0] = kTransparentRed;
    pixel[1] = kTransparentGreen;
    pixel[2] = kTransparentBlue;
  }

  // One slot is always reserved: index 255 is the transparent entry the
  // engine masks out for `{` textures, so no quantized color may land there
  // (an opaque image using that index would spray holes).
  framework::ImageLoader::IndexedImage indexed;
  if (!framework::ImageLoader::quantizeIndexed(sized, kPaletteSize - 1,
                                               indexed)) {
    error = "color quantization failed";
    return false;
  }
  if (!indexed.valid() || indexed.Width != width || indexed.Height != height) {
    error = "unexpected quantized size";
    return false;
  }

  // Palette: always 256 entries, index 255 reserved for transparency. The
  // quantizer ran with 255 colours, so the reserved entry is free by
  // construction - unlike the original, which relied on WuQuant producing an
  // exact blue entry and shipped a broken palette when it did not.
  std::vector<framework::ImageLoader::PaletteColor> palette(
      kPaletteSize, {kTransparentRed, kTransparentGreen, kTransparentBlue});
  if (indexed.Palette.size() > palette.size()) {
    error = "quantized palette is larger than 256 entries";
    return false;
  }
  std::copy(indexed.Palette.begin(), indexed.Palette.end(), palette.begin());
  if (hasTransparency) {
    // Every masked pixel becomes the reserved transparent index, whatever
    // the quantizer chose for the forced blue.
    for (std::size_t i = 0; i < mask.size(); ++i)
      if (mask[i])
        indexed.Indices[i] = static_cast<unsigned char>(kPaletteSize - 1);
  }

  // Four mip levels, each one quarter of the previous area (the 16-pixel
  // alignment above keeps every level integral).
  std::vector<unsigned char> mips;
  mips.reserve(indexed.Indices.size() * 4 / 3 + kMipLevels);
  for (int level = 0; level < kMipLevels; ++level) {
    const int scale = 1 << level;
    for (int y = 0; y < height / scale; ++y)
      for (int x = 0; x < width / scale; ++x)
        mips.push_back(
            indexed.Indices[static_cast<std::size_t>(y * scale) * width +
                            x * scale]);
  }

  // WAD3: one lump holding the miptex (header, mips, palette) and the lump
  // directory record behind it.
  const unsigned mipTexSize = static_cast<unsigned>(
      kMipTexHeaderSize + mips.size() + 2 + kPaletteSize * 3);
  std::vector<unsigned char> bytes;
  bytes.reserve(12 + mipTexSize + kWadLumpSize + 4);
  bytes.insert(bytes.end(), {'W', 'A', 'D', '3'});
  writeU32(bytes, 1); // lump count
  writeU32(bytes, 0); // lump directory offset (patched below)
  // BSPMipTexHeader_t
  for (char c : kLumpName) // 16 bytes, zero padded
    bytes.push_back(static_cast<unsigned char>(c));
  writeU32(bytes, static_cast<unsigned>(width));
  writeU32(bytes, static_cast<unsigned>(height));
  const unsigned mip0 = kMipTexHeaderSize;
  const unsigned mip1 = mip0 + static_cast<unsigned>(width) * height;
  const unsigned mip2 = mip1 + static_cast<unsigned>(width) * height / 4;
  const unsigned mip3 = mip2 + static_cast<unsigned>(width) * height / 16;
  writeU32(bytes, mip0);
  writeU32(bytes, mip1);
  writeU32(bytes, mip2);
  writeU32(bytes, mip3);
  bytes.insert(bytes.end(), mips.begin(), mips.end());
  writeU16(bytes, kPaletteSize); // colour count
  for (const auto &color : palette) {
    bytes.push_back(color.Red);
    bytes.push_back(color.Green);
    bytes.push_back(color.Blue);
  }
  padTo4(bytes);
  const unsigned lumpOffset = static_cast<unsigned>(bytes.size());
  // WAD3Lump_t: data offset, size on disk, size, type (0x43 = miptex),
  // compression, padding, name. The sizes carry the lump's own padding (the
  // reference tempdecal.wad files do the same).
  writeU32(bytes, 12);
  writeU32(bytes, lumpOffset - 12);
  writeU32(bytes, lumpOffset - 12);
  bytes.push_back(0x43);
  bytes.push_back(0);
  writeU16(bytes, 0);
  for (char c : kLumpName) // 16 bytes, zero padded
    bytes.push_back(static_cast<unsigned char>(c));
  // Patch the header's directory pointer.
  bytes[8] = static_cast<unsigned char>(lumpOffset & 0xFF);
  bytes[9] = static_cast<unsigned char>((lumpOffset >> 8) & 0xFF);
  bytes[10] = static_cast<unsigned char>((lumpOffset >> 16) & 0xFF);
  bytes[11] = static_cast<unsigned char>((lumpOffset >> 24) & 0xFF);

  output.Bytes = std::move(bytes);
  // Result preview: the quantized pixels with the transparent entry cleared.
  output.Preview.Width = width;
  output.Preview.Height = height;
  output.Preview.Pixels.resize(static_cast<std::size_t>(width) * height * 4);
  for (std::size_t i = 0; i < indexed.Indices.size(); ++i) {
    unsigned char *pixel = output.Preview.Pixels.data() + i * 4;
    const unsigned char index = indexed.Indices[i];
    if (index == kPaletteSize - 1) {
      pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
      continue;
    }
    const auto &color = palette[index];
    pixel[0] = color.Red;
    pixel[1] = color.Green;
    pixel[2] = color.Blue;
    pixel[3] = 255;
  }
  output.Width = width;
  output.Height = height;
  output.ColorCount = kPaletteSize;
  output.HasTransparency = hasTransparency;
  return true;
}

} // namespace services::spray
