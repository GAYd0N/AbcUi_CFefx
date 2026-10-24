#pragma once

// Module-side additive HUD ink. No pre-generated files or framework font
// atlas contract: each consumer rasterizes and owns its context textures.
#include <abcui/module_api.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace moduleui {
class Glyphs {
public:
  void reset() {
    Uris = {};
    Handles = {};
  }

  std::string ink(const AbcUiModuleApi *api, const std::string &text,
                  const char *tint = "rgb(255, 176, 0)") {
    for (const char c : text)
      if (index(c) < 0)
        return {};
    if (!ensure(api))
      return {};
    std::string result;
    for (const char c : text) {
      result += "<span class=\"hud-glyph\"><img class=\"glyph-ink\" src=\"";
      result += Uris[index(c)];
      result += "\" style=\"image-color: ";
      result += tint;
      result += "\"/></span>";
    }
    return result;
  }

private:
  static constexpr int kWidth = 82;
  static constexpr int kHeight = 128;
  static constexpr char kCharacters[] = "0123456789/x%";
  std::array<std::string, 13> Uris{};
  std::array<AbcUiTexture, 13> Handles{};

  static int index(char c) {
    if (c == 'X')
      c = 'x';
    for (int i = 0; i < 13; ++i)
      if (kCharacters[i] == c)
        return i;
    return -1;
  }

  bool ensure(const AbcUiModuleApi *api) {
    if (!Uris[0].empty()) {
      char uri[192]{};
      if (api->textures->uri(Handles[0], uri, sizeof(uri)) == ABCUI_OK)
        return true;
      reset();
    }
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
      return false;
    HFONT font =
        CreateFontW(-112, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Verdana");
    if (!font) {
      DeleteDC(dc);
      return false;
    }
    HGDIOBJ previous = SelectObject(dc, font);
    MAT2 matrix{};
    matrix.eM11.value = matrix.eM22.value = 1;
    bool complete = true;
    for (int i = 0; i < 13; ++i) {
      GLYPHMETRICS metrics{};
      const DWORD size = GetGlyphOutlineW(dc, kCharacters[i], GGO_GRAY8_BITMAP,
                                          &metrics, 0, nullptr, &matrix);
      if (size == GDI_ERROR || !size) {
        complete = false;
        break;
      }
      std::vector<uint8_t> coverage(size);
      if (GetGlyphOutlineW(dc, kCharacters[i], GGO_GRAY8_BITMAP, &metrics, size,
                           coverage.data(), &matrix) == GDI_ERROR) {
        complete = false;
        break;
      }
      std::vector<uint8_t> rgba(kWidth * kHeight * 4);
      const int stride = (static_cast<int>(metrics.gmBlackBoxX) + 3) & ~3;
      const int sourceWidth = static_cast<int>(metrics.gmBlackBoxX);
      const int fittedWidth = std::min(70, sourceWidth);
      const int left = (kWidth - fittedWidth) / 2;
      const int top = 104 - metrics.gmptGlyphOrigin.y;
      for (int y = 0; y < static_cast<int>(metrics.gmBlackBoxY); ++y) {
        for (int x = 0; x < fittedWidth; ++x) {
          const int dx = left + x, dy = top + y;
          if (dx < 0 || dx >= kWidth || dy < 0 || dy >= kHeight)
            continue;
          auto *pixel = rgba.data() + (dy * kWidth + dx) * 4;
          pixel[0] = pixel[1] = pixel[2] = 255;
          const int sourceX = x * sourceWidth / fittedWidth;
          pixel[3] = static_cast<uint8_t>(
              std::min(255, coverage[y * stride + sourceX] * 255 / 64));
        }
      }
      if (api->textures->upload(rgba.data(), static_cast<uint32_t>(rgba.size()),
                                kWidth, kHeight, 1, &Handles[i]) != ABCUI_OK) {
        complete = false;
        break;
      }
      char uri[192]{};
      if (api->textures->uri(Handles[i], uri, sizeof(uri)) != ABCUI_OK) {
        complete = false;
        break;
      }
      Uris[i] = uri;
    }
    SelectObject(dc, previous);
    DeleteObject(font);
    DeleteDC(dc);
    if (!complete) {
      for (auto handle : Handles)
        api->textures->release(handle);
      reset();
    }
    return complete;
  }
};
} // namespace moduleui
