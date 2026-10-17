// data::sprites implementation - see SpriteResource.h.
// from: ABCEnchance/src/core/resource/spriteresource.* (hud.txt half);
// .spr pixel decoding per SPRView.Net Core/SprReader.cs + FrameDecoder.cs.
#include <metahook.h>

#include "SpriteResource.h"
#include "../framework/ResourceLimits.h"
#include "../framework/ResourceFile.h"
#include <unordered_set>

#include <wrect.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

#include <abc/Console.h>
#include <abc/Paths.h>

#include "render/Render.h"
#include "../rmlui/VGUI2ExtensionImport.h"

namespace sprites {

namespace {

constexpr int kSprSignature = 0x50534449; // "IDSP"
constexpr int kSprVersion = 2;

enum SpriteFormat {
	kFormatNormal = 0,
	kFormatAdditive = 1,
	kFormatIndexAlpha = 2,
	kFormatAlphaTest = 3,
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

// A standalone .spr file kept byte-resident; frames decode + upload lazily
// (batch B water splash: 33-38 frames of 256x512 - keeping every decoded
// frame resident would hold ~40MB of RGBA for two sprites).
// Per-frame dimensions: the .spr frame table allows frames of different
// sizes (header MaxWidth/MaxHeight exist for exactly that), so allocation
// and upload must ride each frame's own geometry.
struct StandaloneSpr {
	std::vector<unsigned char> file;
	std::vector<size_t> frameOffsets;
	std::vector<size_t> framePixelCounts; // pixels per frame (w*h)
	std::vector<int> frameWidths;
	std::vector<int> frameHeights;
	int width = 0;
	int height = 0;
	int format = 0;
};

std::unordered_map<std::string, HudEntry> HudEntries;
std::unordered_map<std::string, DecodedAtlas> Atlases;
std::unordered_map<std::string, int> CropTextures;
std::unordered_map<std::string, StandaloneSpr> Standalone;
std::unordered_map<std::string, int> FrameTextures;
bool ChannelWarned = false;
int HudResolution = 0;

// SPR_GetList paths are relative to the sprite root, except when the list
// escaped it ("../abc/..."): the engine then returns the normalized path
// ("./abc/..."). Try the standard "sprites/" form first, with the already
// normalized path as the fallback for those engine-supplied entries.
bool readSpriteFile(const std::string& atlasFile, std::vector<unsigned char>& bytes) {
	std::string normalized;
	if (!framework::ResourceFile::normalize(atlasFile, normalized))
		return false;

	if (normalized.starts_with("sprites/"))
		return framework::ResourceFile::read(normalized, bytes, framework::ResourceLimits::MaxFileBytes);

	const bool escapedSpriteRoot = atlasFile.starts_with("./") ||
		atlasFile.starts_with(".\\") || atlasFile.starts_with("../") ||
		atlasFile.starts_with("..\\") || atlasFile.find("/../") != std::string::npos ||
		atlasFile.find("\\..\\") != std::string::npos;
	if (escapedSpriteRoot &&
		framework::ResourceFile::read(normalized, bytes, framework::ResourceLimits::MaxFileBytes))
		return true;

	if (framework::ResourceFile::read("sprites/" + normalized, bytes, framework::ResourceLimits::MaxFileBytes))
		return true;
	return framework::ResourceFile::read(normalized, bytes, framework::ResourceLimits::MaxFileBytes);
}

// Forward decl: standalone frame uploads share the atlas upload helper
// (defined below the loaders).
int createTexture(const DecodedAtlas& atlas);

void decodeFramePixels(const unsigned char* indexed, int pixelCount, const unsigned char* palette,
	int paletteSize, int format, unsigned char* rgba) {
	const bool fullPalette = paletteSize == 256;
	const bool indexIsAlpha = format == kFormatIndexAlpha;
	const bool blackIsEmpty = format == kFormatAdditive;
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
		}
		else if ((blackIsEmpty && r == 0 && g == 0 && b == 0) ||
				 (fullPalette && r == keyR && g == keyG && b == keyB)) {
			a = 0;
		}
		unsigned char* px = rgba + i * 4;
		px[0] = r;
		px[1] = g;
		px[2] = b;
		px[3] = a;
	}
}

bool loadSpr(const std::string& atlasFile, DecodedAtlas& out) {
	std::vector<unsigned char> file;
	if (!readSpriteFile(atlasFile, file)) {
		abc::ConsoleWarn("[AbcUi] sprite atlas not found: sprites/%s\n", atlasFile.c_str());
		return false;
	}

	const auto readInt = [&file](size_t offset, int& value) {
		if (!framework::ResourceLimits::span(offset,4,file.size()))
			return false;
		value = static_cast<int>(file[offset]) | (static_cast<int>(file[offset + 1]) << 8) |
			(static_cast<int>(file[offset + 2]) << 16) | (static_cast<int>(file[offset + 3]) << 24);
		return true;
	};

	int signature = 0, version = 0, format = 0;
	if (!readInt(0, signature) || signature != kSprSignature || !readInt(4, version) ||
		version != kSprVersion || !readInt(12, format) || format < 0 || format > 3) {
		abc::ConsoleWarn("[AbcUi] unsupported sprite format: sprites/%s\n", atlasFile.c_str());
		return false;
	}

	int frameCount = 0;
	if (!readInt(28, frameCount) || frameCount <= 0 || frameCount > 4096) {
		abc::ConsoleWarn("[AbcUi] sprite declares no frames: sprites/%s\n", atlasFile.c_str());
		return false;
	}

	size_t offset = 40;
	int paletteSize = 0;
	if (offset + 2 > file.size())
		return false;
	paletteSize = static_cast<int>(file[offset]) | (static_cast<int>(file[offset + 1]) << 8);
	offset += 2;
	if (paletteSize <= 0 || paletteSize > 256 || offset + static_cast<size_t>(paletteSize) * 3 > file.size()) {
		abc::ConsoleWarn("[AbcUi] sprite palette corrupt: sprites/%s\n", atlasFile.c_str());
		return false;
	}
	const unsigned char* palette = file.data() + offset;
	offset += static_cast<size_t>(paletteSize) * 3;

	// First readable frame; malformed tails end the table (SPRView rule -
	// e.g. SC's misc/wei.spr declares 22 frames, the last one is junk).
	int frameW = 0, frameH = 0;
	const unsigned char* pixels = nullptr;
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
		if (!framework::ResourceLimits::imageBytes(frameW,frameH,byteCount)) break;
		const size_t pixelCount = byteCount / 4;
		if (!framework::ResourceLimits::span(offset,pixelCount,file.size()))
			break;
		pixels = file.data() + offset;
		offset += pixelCount;
	}
	if (!pixels) {
		abc::ConsoleWarn("[AbcUi] sprite has no readable frame: sprites/%s\n", atlasFile.c_str());
		return false;
	}

	out.width = frameW;
	out.height = frameH;
	out.rgba.resize(static_cast<size_t>(frameW) * frameH * 4);
	decodeFramePixels(pixels, frameW * frameH, palette, paletteSize, format, out.rgba.data());
	return true;
}

// Parses a standalone .spr (mod-relative path, no sprites/ prefix) into the
// resident frame table: header + per-frame offsets, pixels decoded lazily.
// Sequence (type 0) frames only - group frames are an engine angle-picker
// feature the HUD never used (warn + stop on first group entry).
StandaloneSpr* ensureStandalone(const std::string& relativePath) {
	const auto itor = Standalone.find(relativePath);
	if (itor != Standalone.end())
		return &itor->second;

	// Negative-result marker: a failed parse is remembered as an empty entry
	// (no re-parse per frame) that does NOT retain the file bytes - a
	// corrupt/huge file must not squat in memory until shutdown.
	const auto fail = [&relativePath]() {
		Standalone.emplace(relativePath, StandaloneSpr{});
		return static_cast<StandaloneSpr*>(nullptr);
	};

	StandaloneSpr spr;
	if (!framework::ResourceFile::read(relativePath, spr.file, framework::ResourceLimits::MaxFileBytes)) {
		abc::ConsoleWarn("[AbcUi] sprite not found: %s\n", relativePath.c_str());
		return fail();
	}

	const auto readInt = [&spr](size_t offset, int& value) {
		if (!framework::ResourceLimits::span(offset,4,spr.file.size()))
			return false;
		value = static_cast<int>(spr.file[offset]) | (static_cast<int>(spr.file[offset + 1]) << 8) |
			(static_cast<int>(spr.file[offset + 2]) << 16) | (static_cast<int>(spr.file[offset + 3]) << 24);
		return true;
	};

	int signature = 0, version = 0;
	if (!readInt(0, signature) || signature != kSprSignature || !readInt(4, version) ||
		version != kSprVersion || !readInt(12, spr.format) || spr.format < 0 || spr.format > 3) {
		abc::ConsoleWarn("[AbcUi] unsupported sprite format: %s\n", relativePath.c_str());
		return fail();
	}
	int frameCount = 0;
	if (!readInt(28, frameCount) || frameCount <= 0 || frameCount > 4096) {
		abc::ConsoleWarn("[AbcUi] sprite declares no frames: %s\n", relativePath.c_str());
		return fail();
	}

	size_t offset = 40;
	if (offset + 2 > spr.file.size())
		return fail();
	int paletteSize = static_cast<int>(spr.file[offset]) | (static_cast<int>(spr.file[offset + 1]) << 8);
	offset += 2 + static_cast<size_t>(paletteSize) * 3;
	if (paletteSize <= 0 || paletteSize > 256 || offset > spr.file.size()) {
		abc::ConsoleWarn("[AbcUi] sprite palette corrupt: %s\n", relativePath.c_str());
		return fail();
	}

	bool usable = false;
	for (int i = 0; i < frameCount; i++) {
		int fields[5];
		bool truncated = false;
		for (int f = 0; f < 5; f++) {
			if (!readInt(offset + f * 4, fields[f])) {
				truncated = true;
				break;
			}
		}
		if (truncated)
			break; // malformed tail: end the table (SPRView rule)
		if (fields[0] != 0) {
			abc::ConsoleWarn("[AbcUi] group frame unsupported, stopping at %d: %s\n", i, relativePath.c_str());
			break;
		}
		const int frameW = fields[3];
		const int frameH = fields[4];
		size_t byteCount;
		if (!framework::ResourceLimits::imageBytes(frameW,frameH,byteCount)) break;
		const size_t pixelCount = byteCount / 4;
		if (!framework::ResourceLimits::span(offset,20,spr.file.size()) || !framework::ResourceLimits::span(offset+20,pixelCount,spr.file.size()))
			break;
		spr.frameOffsets.push_back(offset + 20);
		spr.framePixelCounts.push_back(pixelCount);
		spr.frameWidths.push_back(frameW);
		spr.frameHeights.push_back(frameH);
		if (!usable) {
			spr.width = frameW;
			spr.height = frameH;
			usable = true;
		}
		offset += 20 + pixelCount;
	}
	if (!usable) {
		abc::ConsoleWarn("[AbcUi] sprite has no readable frame: %s\n", relativePath.c_str());
		return fail();
	}
	return &Standalone.emplace(relativePath, std::move(spr)).first->second;
}

// Decodes + uploads one frame of a standalone sprite (id cached per URI key).
// Dimensions come from the frame's own entry - a later frame may legitimately
// differ from the first one (F audit: allocating by frame 0 while writing by
// the requested frame's pixel count overflowed the buffer).
int standaloneFrameTexture(const std::string& relativePath, int frame, int& width, int& height) {
	const StandaloneSpr* spr = ensureStandalone(relativePath);
	if (!spr || frame < 0 || frame >= static_cast<int>(spr->frameOffsets.size()))
		return 0;
	width = spr->frameWidths[frame];
	height = spr->frameHeights[frame];

	const std::string key = relativePath + "|" + std::to_string(frame);
	const auto cached = FrameTextures.find(key);
	if (cached != FrameTextures.end())
		return cached->second;

	std::vector<unsigned char> rgba(static_cast<size_t>(width) * height * 4);
	// Palette follows the 40-byte header (fixed layout, see decodeFramePixels
	// callers); re-read its size from the same spot every frame.
	const int paletteSize =
		static_cast<int>(spr->file[40]) | (static_cast<int>(spr->file[41]) << 8);
	decodeFramePixels(spr->file.data() + spr->frameOffsets[frame],
		static_cast<int>(spr->framePixelCounts[frame]), spr->file.data() + 42,
		paletteSize, spr->format, rgba.data());

	DecodedAtlas atlas;
	atlas.width = width;
	atlas.height = height;
	atlas.rgba = std::move(rgba);
	const int id = createTexture(atlas);
	if (id)
		FrameTextures[key] = id;
	return id;
}

int createTexture(const DecodedAtlas& atlas) {
	if (!vgui2ext::available()) {
		if (!ChannelWarned) {
			ChannelWarned = true;
			abc::ConsoleWarn("[AbcUi] sprite textures unavailable: VGUI2Extension missing\n");
		}
		return 0;
	}
	const int id = vgui2ext::createTexture();
	if (!id)
		return 0;
	vgui2ext::uploadTexture(id, atlas.rgba.data(), atlas.width, atlas.height);
	return id;
}

DecodedAtlas* ensureAtlas(const std::string& atlasFile) {
	const auto key = atlasFile;
	auto itor = Atlases.find(key);
	if (itor != Atlases.end())
		return &itor->second;
	DecodedAtlas atlas;
	if (!loadSpr(key, atlas))
		return nullptr;
	return &Atlases.emplace(key, std::move(atlas)).first->second;
}

} // namespace

void vidInit() {
	const int screenW = abc::MetaRenderer() ? static_cast<int>(abc::MetaRenderer()->GetSwapChainWidth()) : 1920;
	const int wantedRes = screenW < 640 ? 320 : 640;
	if (HudResolution == wantedRes && !HudEntries.empty()) return;
	HudEntries.clear();
	HudResolution = wantedRes;
	// Same engine-side parse the old gSpriteRes used; entries below the 640
	// res band are skipped exactly like the old filter (screen < 640 wide
	// selects the 320 set - irrelevant for the 1080p+ HUD, keep 640 only
	// when the screen is small).
	int count = 0;
	client_sprite_t* list = gEngfuncs.pfnSPR_GetList(const_cast<char*>("sprites/hud.txt"), &count);
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
	for (const auto& [key,id] : CropTextures) ids.insert(id);
	for (const auto& [key,id] : FrameTextures) ids.insert(id);
	for (int id : ids) if (id) vgui2ext::deleteTextureById(id);
	CropTextures.clear();
	Atlases.clear();
	HudEntries.clear();
	HudResolution = 0;
	Standalone.clear();
	FrameTextures.clear();
}

bool findHudSprite(const char* name, std::string& atlasFile, wrect_t& rect) {
	const auto itor = HudEntries.find(name);
	if (itor == HudEntries.end())
		return false;
	atlasFile = itor->second.atlasFile;
	rect = itor->second.rect;
	return true;
}

int croppedTexture(const std::string& atlasFile, const wrect_t& rect, bool straightAlpha) {
	DecodedAtlas* atlas = ensureAtlas(atlasFile);
	if (!atlas) return 0;
	int left, top, width, height;
	if (!framework::ResourceLimits::crop(rect.left,rect.top,rect.right,rect.bottom,atlas->width,atlas->height,left,top,width,height)) return 0;

	// Scheme tag keeps the two blend flavors of one crop in separate texture
	// ids (the additive mark rides the id - see AbcRenderInterface).
	const std::string key = (straightAlpha ? "a|" : "add|") + atlasFile + "|" +
		std::to_string(left) + "," + std::to_string(top) + "," +
		std::to_string(width) + "," + std::to_string(height);
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
			atlas->rgba.data() + (static_cast<size_t>(srcY) * atlas->width + left) * 4,
			static_cast<size_t>(copyW) * 4);
	}
	const int id = createTexture(crop);
	if (id)
		CropTextures[key] = id;
	return id;
}

int textureForUri(const std::string& uri, int& width, int& height) {
	if (uri.rfind("spr://", 0) == 0) {
		std::string atlasFile;
		wrect_t rect{};
		if (!findHudSprite(uri.c_str() + 6, atlasFile, rect))
			return 0;
		const auto* atlas = ensureAtlas(atlasFile);
		int x, y;
		if (!atlas || !framework::ResourceLimits::crop(rect.left,rect.top,rect.right,rect.bottom,atlas->width,atlas->height,x,y,width,height)) return 0;
		return croppedTexture(atlasFile, rect, false);
	}
	if (uri.rfind("spricon://", 0) == 0) {
		// spricon://<atlas>/<l>,<t>,<r>,<b>
		const size_t slash = uri.find('/', 10);
		if (slash == std::string::npos)
			return 0;
		const std::string atlasFile = uri.substr(10, slash - 10);
		wrect_t rect{};
		if (sscanf_s(uri.c_str() + slash + 1, "%d,%d,%d,%d", &rect.left, &rect.top, &rect.right, &rect.bottom) != 4)
			return 0;
		const auto* atlas = ensureAtlas(atlasFile);
		int x, y;
		if (!atlas || !framework::ResourceLimits::crop(rect.left,rect.top,rect.right,rect.bottom,atlas->width,atlas->height,x,y,width,height)) return 0;
		return croppedTexture(atlasFile, rect, false);
	}
	if (uri.rfind("spralpha://", 0) == 0) {
		// spralpha://<atlas>/<l>,<t>,<r>,<b> - straight-alpha crop (old
		// kRenderTransTexture sprites: the weapon crosshairs).
		const size_t slash = uri.find('/', 11);
		if (slash == std::string::npos)
			return 0;
		const std::string atlasFile = uri.substr(11, slash - 11);
		wrect_t rect{};
		if (sscanf_s(uri.c_str() + slash + 1, "%d,%d,%d,%d", &rect.left, &rect.top, &rect.right, &rect.bottom) != 4)
			return 0;
		const auto* atlas = ensureAtlas(atlasFile);
		int x, y;
		if (!atlas || !framework::ResourceLimits::crop(rect.left,rect.top,rect.right,rect.bottom,atlas->width,atlas->height,x,y,width,height)) return 0;
		return croppedTexture(atlasFile, rect, true);
	}
	if (uri.rfind("sprframe://", 0) == 0) {
		// sprframe://<path>|<frame>
		const size_t pipe = uri.rfind('|');
		if (pipe == std::string::npos)
			return 0;
		const std::string path = uri.substr(11, pipe - 11);
		const int frame = atoi(uri.c_str() + pipe + 1);
		return standaloneFrameTexture(path, frame, width, height);
	}
	return 0;
}

int standaloneFrameCount(const std::string& relativePath) {
	const StandaloneSpr* spr = ensureStandalone(relativePath);
	return spr ? static_cast<int>(spr->frameOffsets.size()) : 0;
}

}
