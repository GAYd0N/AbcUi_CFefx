// data::sprites implementation - see SpriteResource.h.
// from: ABCEnchance/src/core/resource/spriteresource.* (hud.txt half);
// .spr pixel decoding per SPRView.Net Core/SprReader.cs + FrameDecoder.cs.
#include <metahook.h>
#include <cstddef>

#include "SpriteResource.h"
#include <abcui/module_api.h>
#include "SpriteUri.h"
#include "modsys/ModuleVfs.h"
#include "../../framework/ResourceLimits.h"
#include "../../framework/ResourceFile.h"
#include <unordered_set>

#include <wrect.h>

#include <cstring>
#include <vector>

#include <abc/Console.h>
#include <abc/Paths.h>

#include "framework/ScreenMetrics.h"
#include "../../framework/TextureSink.h"

namespace sprites {

namespace {

constexpr int kSprSignature = 0x50534449; // "IDSP"
constexpr int kSprVersion = 2;

enum class SpriteFormat {
  kNormal = 0,
  kAdditive = 1,
  kIndexAlpha = 2,
  kAlphaTest = 3,
};

struct HudEntry {
  std::string atlasFile;
  wrect_t rect;
};

struct DecodedAtlas {
  int width = 0;
  int height = 0;
  std::vector<unsigned char> rgba; // top-down RGBA8888
};

std::unordered_map<std::string, HudEntry> HudEntries;
std::unordered_map<std::string, DecodedAtlas> Atlases;
std::unordered_map<std::string, int> &atlasErrors() {
  static std::unordered_map<std::string, int> instance;
  return instance;
}
std::unordered_map<std::string, int> CropTextures;
bool ChannelWarned = false;
int HudResolution = 0;

// Native sprite paths use the engine's sprite root; module-owned fallback
// atlases have explicit mod:// URIs and never fall through to engine files.
bool readSpriteFile(const std::string &atlasFile,
                    std::vector<std::byte> &bytes) {
  if (framework::ModuleVfs::isModuleUri(atlasFile))
    return framework::ResourceFile::read(
        atlasFile, bytes, framework::ResourceLimits::MaxFileBytes);
  std::string normalized;
  if (atlasFile.starts_with("sprites/") || atlasFile.starts_with("sprites\\")) {
    if (!framework::ResourceFile::normalize(atlasFile, normalized))
      return false;
    return framework::ResourceFile::read(
        normalized, bytes, framework::ResourceLimits::MaxFileBytes);
  }

  std::string spritePath;
  if (framework::ResourceFile::normalize("sprites/" + atlasFile, spritePath) &&
      framework::ResourceFile::read(spritePath, bytes,
                                    framework::ResourceLimits::MaxFileBytes))
    return true;

  // Fallback for paths already rooted at the mod directory.
  if (!framework::ResourceFile::normalize(atlasFile, normalized))
    return false;
  return framework::ResourceFile::read(normalized, bytes,
                                       framework::ResourceLimits::MaxFileBytes);
}

void decodeFramePixels(const unsigned char *indexed, int pixelCount,
                       const unsigned char *palette, int paletteSize,
                       int format, unsigned char *rgba) {
  const bool fullPalette = paletteSize == 256;
  const bool indexIsAlpha =
      format == static_cast<int>(SpriteFormat::kIndexAlpha);
  const bool blackIsEmpty = format == static_cast<int>(SpriteFormat::kAdditive);
  // Key colour = palette entry 255 (colour compare, not index compare) for
  // every non-IndexAlpha format with a full palette (SPRView rule).
  unsigned char keyR = 0, keyG = 0, keyB = 0;
  if (!indexIsAlpha && fullPalette) {
    keyR = palette[255 * 3 + 0];
    keyG = palette[255 * 3 + 1];
    keyB = palette[255 * 3 + 2];
  }
  for (int i = 0; i < pixelCount; i++) {
    const int index = indexed[i];
    const int entry = (index < paletteSize ? index : paletteSize - 1) * 3;
    unsigned char r = palette[entry + 0];
    unsigned char g = palette[entry + 1];
    unsigned char b = palette[entry + 2];
    unsigned char a = 255;
    if (indexIsAlpha) {
      a = static_cast<unsigned char>(index);
    } else if ((blackIsEmpty && r == 0 && g == 0 && b == 0) ||
               (fullPalette && r == keyR && g == keyG && b == keyB)) {
      a = 0;
    }
    unsigned char *px = rgba + i * 4;
    px[0] = r;
    px[1] = g;
    px[2] = b;
    px[3] = a;
  }
}

bool loadSpr(const std::string &atlasFile, DecodedAtlas &out) {
  std::vector<std::byte> file;
  if (!readSpriteFile(atlasFile, file)) {
    abc::ConsoleWarn("[AbcUi] sprite atlas not found: sprites/%s\n",
                     atlasFile.c_str());
    return false;
  }

  const auto readInt = [&file](size_t offset, int &value) {
    if (!framework::ResourceLimits::span(offset, 4, file.size()))
      return false;
    value = static_cast<int>(std::to_integer<unsigned char>(file[offset])) |
            (static_cast<int>(std::to_integer<unsigned char>(file[offset + 1]))
             << 8) |
            (static_cast<int>(std::to_integer<unsigned char>(file[offset + 2]))
             << 16) |
            (static_cast<int>(std::to_integer<unsigned char>(file[offset + 3]))
             << 24);
    return true;
  };

  int signature = 0, version = 0, format = 0;
  if (!readInt(0, signature) || signature != kSprSignature ||
      !readInt(4, version) || version != kSprVersion || !readInt(12, format) ||
      format < 0 || format > 3) {
    abc::ConsoleWarn("[AbcUi] unsupported sprite format: sprites/%s\n",
                     atlasFile.c_str());
    return false;
  }

  int frameCount = 0;
  if (!readInt(28, frameCount) || frameCount <= 0 || frameCount > 4096) {
    abc::ConsoleWarn("[AbcUi] sprite declares no frames: sprites/%s\n",
                     atlasFile.c_str());
    return false;
  }

  size_t offset = 40;
  int paletteSize = 0;
  if (offset + 2 > file.size())
    return false;
  paletteSize =
      static_cast<int>(std::to_integer<unsigned char>(file[offset])) |
      (static_cast<int>(std::to_integer<unsigned char>(file[offset + 1])) << 8);
  offset += 2;
  if (paletteSize <= 0 || paletteSize > 256 ||
      offset + static_cast<size_t>(paletteSize) * 3 > file.size()) {
    abc::ConsoleWarn("[AbcUi] sprite palette corrupt: sprites/%s\n",
                     atlasFile.c_str());
    return false;
  }
  const unsigned char *palette =
      reinterpret_cast<const unsigned char *>(file.data()) + offset;
  offset += static_cast<size_t>(paletteSize) * 3;

  // First readable frame; malformed tails end the table (SPRView rule -
  // e.g. SC's misc/wei.spr declares 22 frames, the last one is junk).
  int frameW = 0, frameH = 0;
  const unsigned char *pixels = nullptr;
  for (int i = 0; i < frameCount && pixels == nullptr; i++) {
    int fields[5];
    bool truncated = false;
    for (int f = 0; f < 5; f++) {
      if (!readInt(offset + f * 4, fields[f])) {
        truncated = true;
        break;
      }
    }
    if (truncated || fields[0] != 0)
      break;
    offset += 20;
    frameW = fields[3];
    frameH = fields[4];
    size_t byteCount;
    if (!framework::ResourceLimits::imageBytes(frameW, frameH, byteCount))
      break;
    const size_t pixelCount = byteCount / 4;
    if (!framework::ResourceLimits::span(offset, pixelCount, file.size()))
      break;
    pixels = reinterpret_cast<const unsigned char *>(file.data()) + offset;
    offset += pixelCount;
  }
  if (!pixels) {
    abc::ConsoleWarn("[AbcUi] sprite has no readable frame: sprites/%s\n",
                     atlasFile.c_str());
    return false;
  }

  out.width = frameW;
  out.height = frameH;
  out.rgba.resize(static_cast<size_t>(frameW) * frameH * 4);
  decodeFramePixels(pixels, frameW * frameH, palette, paletteSize, format,
                    out.rgba.data());
  return true;
}

int createTexture(const DecodedAtlas &atlas) {
  if (!framework::texture::available()) {
    if (!ChannelWarned) {
      ChannelWarned = true;
      abc::ConsoleWarn(
          "[AbcUi] sprite textures unavailable: VGUI2Extension missing\n");
    }
    return 0;
  }
  const int id = framework::texture::create();
  if (!id)
    return 0;
  framework::texture::upload(id, atlas.rgba.data(), atlas.width, atlas.height);
  return id;
}

DecodedAtlas *ensureAtlas(const std::string &atlasFile) {
  const auto key = atlasFile;
  auto itor = Atlases.find(key);
  if (itor != Atlases.end())
    return &itor->second;
  if (atlasErrors().count(key))
    return nullptr;
  DecodedAtlas atlas;
  if (!loadSpr(key, atlas)) {
    atlasErrors()[key] = ABCUI_ERROR_DECODE;
    return nullptr;
  }
  return &Atlases.emplace(key, std::move(atlas)).first->second;
}

} // namespace

void vidInit() {
  const int screenW = framework::ScreenWidth();
  const int wantedRes = screenW < 640 ? 320 : 640;
  if (HudResolution == wantedRes && !HudEntries.empty())
    return;
  HudEntries.clear();
  HudResolution = wantedRes;
  // Same engine-side parse the old gSpriteRes used; entries below the 640
  // res band are skipped exactly like the old filter (screen < 640 wide
  // selects the 320 set - irrelevant for the 1080p+ HUD, keep 640 only
  // when the screen is small).
  int count = 0;
  client_sprite_t *list =
      gEngfuncs.pfnSPR_GetList(const_cast<char *>("sprites/hud.txt"), &count);
  if (!list)
    return;
  for (int i = 0; i < count; i++) {
    if (list[i].iRes != wantedRes)
      continue;
    HudEntry entry;
    entry.atlasFile = std::string(list[i].szSprite) + ".spr";
    entry.rect = list[i].rc;
    HudEntries[list[i].szName] = std::move(entry);
  }
}

void shutdown() {
  // Rml borrowed records have been released before cache-owned IDs.
  std::unordered_set<int> ids;
  for (const auto &[key, id] : CropTextures)
    ids.insert(id);
  for (int id : ids)
    if (id)
      framework::texture::release(id);
  CropTextures.clear();
  Atlases.clear();
  atlasErrors().clear();
  HudEntries.clear();
  HudResolution = 0;
}

bool findHudSprite(const char *name, std::string &atlasFile, wrect_t &rect) {
  const auto itor = HudEntries.find(name);
  if (itor == HudEntries.end())
    return false;
  atlasFile = itor->second.atlasFile;
  rect = itor->second.rect;
  return true;
}

int resourceStatus(const std::string &atlasFile, const wrect_t &rect) {
  const auto *atlas = ensureAtlas(atlasFile);
  if (!atlas)
    return ABCUI_ERROR_DECODE;
  int x = 0, y = 0, width = 0, height = 0;
  return framework::ResourceLimits::crop(rect.left, rect.top, rect.right,
                                         rect.bottom, atlas->width,
                                         atlas->height, x, y, width, height)
             ? ABCUI_OK
             : ABCUI_ERROR_INVALID;
}

int croppedTexture(const std::string &atlasFile, const wrect_t &rect,
                   bool straightAlpha) {
  DecodedAtlas *atlas = ensureAtlas(atlasFile);
  if (!atlas)
    return 0;
  int left, top, width, height;
  if (!framework::ResourceLimits::crop(rect.left, rect.top, rect.right,
                                       rect.bottom, atlas->width, atlas->height,
                                       left, top, width, height))
    return 0;

  // Scheme tag keeps the two blend flavors of one crop in separate texture
  // ids (the additive mark rides the id - see AbcRenderInterface).
  const std::string key = (straightAlpha ? "a|" : "add|") + atlasFile + "|" +
                          std::to_string(left) + "," + std::to_string(top) +
                          "," + std::to_string(width) + "," +
                          std::to_string(height);
  const auto itor = CropTextures.find(key);
  if (itor != CropTextures.end())
    return itor->second;

  DecodedAtlas crop;
  crop.width = width;
  crop.height = height;
  crop.rgba.resize(static_cast<size_t>(width) * height * 4);
  for (int y = 0; y < height; y++) {
    const int srcY = top + y;
    if (srcY < 0 || srcY >= atlas->height)
      continue;
    const int copyW = width < atlas->width - left ? width : atlas->width - left;
    if (copyW <= 0)
      continue;
    memcpy(crop.rgba.data() + static_cast<size_t>(y) * width * 4,
           atlas->rgba.data() +
               (static_cast<size_t>(srcY) * atlas->width + left) * 4,
           static_cast<size_t>(copyW) * 4);
  }
  const int id = createTexture(crop);
  if (id)
    CropTextures[key] = id;
  return id;
}

int textureForUri(const std::string &uri, int &width, int &height) {
  if (uri.rfind("spr://", 0) == 0) {
    std::string atlasFile;
    wrect_t rect{};
    if (!findHudSprite(uri.c_str() + 6, atlasFile, rect))
      return 0;
    const auto *atlas = ensureAtlas(atlasFile);
    int x, y;
    if (!atlas || !framework::ResourceLimits::crop(
                      rect.left, rect.top, rect.right, rect.bottom,
                      atlas->width, atlas->height, x, y, width, height))
      return 0;
    return croppedTexture(atlasFile, rect, false);
  }
  if (uri.rfind("spricon://", 0) == 0) {
    // spricon://<atlas>/<l>,<t>,<r>,<b>
    std::string atlasFile;
    int values[4] = {0, 0, 0, 0};
    if (!splitCropUri(uri, 10, atlasFile, values))
      return 0;
    wrect_t rect{};
    rect.left = values[0];
    rect.top = values[1];
    rect.right = values[2];
    rect.bottom = values[3];
    const auto *atlas = ensureAtlas(atlasFile);
    int x, y;
    if (!atlas || !framework::ResourceLimits::crop(
                      rect.left, rect.top, rect.right, rect.bottom,
                      atlas->width, atlas->height, x, y, width, height))
      return 0;
    return croppedTexture(atlasFile, rect, false);
  }
  if (uri.rfind("spralpha://", 0) == 0) {
    // spralpha://<atlas>/<l>,<t>,<r>,<b> - straight-alpha crop (old
    // kRenderTransTexture sprites: the weapon crosshairs).
    std::string atlasFile;
    int values[4] = {0, 0, 0, 0};
    if (!splitCropUri(uri, 11, atlasFile, values))
      return 0;
    wrect_t rect{};
    rect.left = values[0];
    rect.top = values[1];
    rect.right = values[2];
    rect.bottom = values[3];
    const auto *atlas = ensureAtlas(atlasFile);
    int x, y;
    if (!atlas || !framework::ResourceLimits::crop(
                      rect.left, rect.top, rect.right, rect.bottom,
                      atlas->width, atlas->height, x, y, width, height))
      return 0;
    return croppedTexture(atlasFile, rect, true);
  }
  return 0;
}

#ifndef NDEBUG
namespace {
int readProbeInt(const std::vector<std::byte> &file, size_t offset, bool &ok) {
  if (offset + 4 > file.size()) {
    ok = false;
    return 0;
  }
  return static_cast<int>(std::to_integer<unsigned char>(file[offset])) |
         (static_cast<int>(std::to_integer<unsigned char>(file[offset + 1]))
          << 8) |
         (static_cast<int>(std::to_integer<unsigned char>(file[offset + 2]))
          << 16) |
         (static_cast<int>(std::to_integer<unsigned char>(file[offset + 3]))
          << 24);
}
} // namespace

void debugDumpAtlas(const std::string &atlasFile) {
  std::string normalized;
  const bool normalizedOk =
      framework::ResourceFile::normalize(atlasFile, normalized);
  std::vector<std::byte> bytes;
  const bool readOk = readSpriteFile(atlasFile, bytes);
  abc::ConsolePrint("[AbcUi] sprdbg atlas='%s' norm='%s' normOk=%d read=%d "
                    "bytes=%u cached=%d\n",
                    atlasFile.c_str(), normalized.c_str(), normalizedOk ? 1 : 0,
                    readOk ? 1 : 0, static_cast<unsigned>(bytes.size()),
                    Atlases.count(atlasFile) ? 1 : 0);
  abc::ConsolePrint(
      "[AbcUi] sprdbg exists raw=%d sprites=%d fs=%p\n",
      normalizedOk && framework::ResourceFile::exists(normalized) ? 1 : 0,
      framework::ResourceFile::exists("sprites/" + normalized) ? 1 : 0,
      static_cast<void *>(abc::g_pFileSystem));
  if (!readOk || bytes.size() < 40)
    return;
  bool ok = true;
  const int signature = readProbeInt(bytes, 0, ok);
  const int version = readProbeInt(bytes, 4, ok);
  const int format = readProbeInt(bytes, 12, ok);
  const int width = readProbeInt(bytes, 20, ok);
  const int height = readProbeInt(bytes, 24, ok);
  const int frames = readProbeInt(bytes, 28, ok);
  abc::ConsolePrint("[AbcUi] sprdbg header ok=%d sig=%x ver=%d fmt=%d "
                    "declared=%dx%d frames=%d\n",
                    ok ? 1 : 0, static_cast<unsigned>(signature), version,
                    format, width, height, frames);
  const auto cached = Atlases.find(atlasFile);
  if (cached != Atlases.end())
    abc::ConsolePrint("[AbcUi] sprdbg decoded=%dx%d rgba=%u\n",
                      cached->second.width, cached->second.height,
                      static_cast<unsigned>(cached->second.rgba.size()));
}

int debugResolveUri(const std::string &uri) {
  int width = 0, height = 0;
  const int id = textureForUri(uri, width, height);
  abc::ConsolePrint("[AbcUi] sprdbg uri='%s' id=%d dims=%dx%d\n", uri.c_str(),
                    id, width, height);
  return id;
}
#endif

} // namespace sprites
