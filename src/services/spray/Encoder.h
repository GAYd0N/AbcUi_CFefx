#pragma once

// services::spray::Encoder - the spraypaint encoder: a decoded image in, the
// complete WAD3 byte image out (the file the engine reads as tempdecal.wad and
// sprays as `{LOGO`). Ported from ABCEnchance VGUI2/GameUI/OptionAdvancedDlg
// OnFileSelected (GetValidateSparySize + the WAD writer), with the palette
// fallback fixed - see encode().
//
// The encoder is engine-free and FreeImage-free: decoding and rescaling run
// through framework::ImageLoader (the plugin's single FreeImage seam), the
// WAD assembly is plain byte work, so the whole path is testable offline.

#include "framework/ImageLoader.h"

#include <string>
#include <vector>

namespace services::spray {

// The brush-size rule of the original: a spray is capped by area, both sides
// keep the 16-pixel alignment (so all four mip levels divide evenly) and the
// smallest side stays at 16. The original checked the cap with two different
// constants (14336 / 14436) that could only differ by one shrink step; this
// port keeps one cap and documents it.
struct SizeLimits {
  int MaxArea = 14336;
  int MaxSide = 256;
  int Align = 16;
  float Shrink = 0.95f;
};

// Fits an arbitrary image size into the spray limits. False for empty sizes.
bool fitSize(int width, int height, const SizeLimits &limits, int &outWidth,
             int &outHeight);

struct EncodedSpray {
  std::vector<unsigned char> Bytes;
  // The encoded pixels as top-down RGBA (transparent index -> alpha 0) for
  // the UI's result preview: what the spray will look like, quantization and
  // all, not the source image.
  framework::ImageLoader::DecodedImage Preview;
  int Width = 0;
  int Height = 0;
  // Palette entries written (always 256; index 255 is the transparent blue
  // when the source had transparent pixels).
  int ColorCount = 0;
  bool HasTransparency = false;
};

// Encodes one image. Fails (with a human-readable reason) on an empty image,
// an unusable size or a failed resize/quantize. Transparency rule: pixels with
// alpha < 125 become the reserved index 255 whose colour is the engine's
// transparent blue (0, 0, 255). The original forced those pixels to blue
// before quantizing, then hoped WuQuant produced an exact blue palette entry
// to swap into slot 255; when it did not, its fallback wrote a red dummy entry
// and left the pixels opaque (a broken spray). This port always quantizes to
// 255 colours and writes the reserved blue itself, so the slot is free by
// construction and opaque pixels can never land on the transparent index.
bool encode(const framework::ImageLoader::DecodedImage &source,
            EncodedSpray &output, std::string &error);

} // namespace services::spray
