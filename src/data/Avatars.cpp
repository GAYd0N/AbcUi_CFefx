// data::avatars implementation - see Avatars.h.
#include <metahook.h>

#include "Avatars.h"

#include <unordered_map>

#include "../framework/ImageLoader.h"
#include "../framework/ResourceLimits.h"
#include "../hooks.h"
#include "../rmlui/SteamAPI.h"
#include "../rmlui/VGUI2ExtensionImport.h"

namespace avatars {

namespace {

struct Entry {
	std::vector<unsigned char> rgba; // uploaded/ready pixels
	int width = 0;
	int height = 0;
	int textureId = 0; // 0 = not uploaded yet
	int generation = 0; // bumped on upload - the scoreboard cache-bust suffix
	double nextAttempt = 0.0; // steam retry throttle (old m_fNextLoadTime)
};

// Monotonic client clock for the throttle; frametime-independent.
double NowSeconds() {
	return static_cast<double>(GetTickCount64()) / 1000.0;
}

std::unordered_map<std::uint64_t, Entry> Entries;
int DefaultAvatarTexture = 0;
int DefaultAvatarWidth = 0;
int DefaultAvatarHeight = 0;

bool loadDefaultAvatar() {
	// Mod-relative image through the shared FreeImage loader (format is
	// detected from the file content, so the shipped asset's format is free
	// to change without touching this layer).
	if (!vgui2ext::available())
		return false;
	framework::ImageLoader::DecodedImage image;
	if (!framework::ImageLoader::loadFromFile("abc/abcui/img/default_avatar.png", image))
		return false;
	DefaultAvatarTexture = vgui2ext::createTexture();
	if (!DefaultAvatarTexture)
		return false;
	vgui2ext::uploadTexture(DefaultAvatarTexture, image.Pixels.data(), image.Width, image.Height);
	DefaultAvatarWidth = image.Width;
	DefaultAvatarHeight = image.Height;
	return true;
}

bool upload(Entry& entry) {
	if (!vgui2ext::available())
		return false;
	if (!entry.textureId)
		entry.textureId = vgui2ext::createTexture();
	if (!entry.textureId)
		return false;
	vgui2ext::uploadTexture(entry.textureId, entry.rgba.data(), entry.width, entry.height);
	return true;
}

// The steamapi fetch attempt behind both consumers (the old CAvatarImage
// 1s throttle, m_fNextLoadTime): GetSmallFriendAvatar returns 0 while steam
// is still downloading, callers re-ask until it lands. True when the RGBA
// uploaded this call.
bool tryFetch(Entry& entry, std::uint64_t steamId64, double now) {
	if (entry.textureId || now < entry.nextAttempt)
		return false;
	entry.nextAttempt = now + 1.0;
	if (entry.rgba.empty()) {
		steamapi::AvatarRgba avatar;
		if (!steamapi::avatar(steamId64, avatar) || !avatar.rgba) return false;
		size_t bytes;
		if (!framework::ResourceLimits::imageBytes(avatar.width,avatar.height,bytes)) return false;
		entry.rgba.assign(avatar.rgba,avatar.rgba+bytes); entry.width=avatar.width; entry.height=avatar.height;
	}
	if (!upload(entry))
		return false;
	entry.generation++;
	return true;
}

}

void init() {
	loadDefaultAvatar();
}

void shutdown() {
	for (const auto& [id,entry] : Entries) if (entry.textureId) vgui2ext::deleteTextureById(entry.textureId);
	if (DefaultAvatarTexture) vgui2ext::deleteTextureById(DefaultAvatarTexture);
	Entries.clear();
	DefaultAvatarTexture = 0;
	DefaultAvatarWidth = 0;
	DefaultAvatarHeight = 0;
}

int textureFor(std::uint64_t steamId64, int& width, int& height, bool triggerFetch) {
	if (!steamId64) {
		width = DefaultAvatarWidth;
		height = DefaultAvatarHeight;
		return DefaultAvatarTexture;
	}

	if (!Entries.count(steamId64) && Entries.size() >= 256) { width=DefaultAvatarWidth; height=DefaultAvatarHeight; return DefaultAvatarTexture; }
	Entry& entry = Entries[steamId64];
	if (entry.textureId && !entry.rgba.empty()) {
		width = entry.width;
		height = entry.height;
		return entry.textureId;
	}

	// Retryable with the old 1s throttle (CAvatarImage m_fNextLoadTime):
	// GetSmallFriendAvatar returns 0 while steam is still fetching, and the
	// throttle keeps the per-refresh polling from hammering the interface.
	if (triggerFetch && tryFetch(entry, steamId64, NowSeconds())) {
		width = entry.width;
		height = entry.height;
		return entry.textureId;
	}

	width = DefaultAvatarWidth;
	height = DefaultAvatarHeight;
	return DefaultAvatarTexture;
}

bool touch(std::uint64_t steamId64) {
	if (!steamId64 || (!Entries.count(steamId64) && Entries.size() >= 256))
		return false;
	return tryFetch(Entries[steamId64], steamId64, NowSeconds());
}

void onFrame() {
	if (!DefaultAvatarTexture) loadDefaultAvatar();
	const double now = NowSeconds();
	for (auto& [id,entry] : Entries) tryFetch(entry,id,now);
}

int generation(std::uint64_t steamId64) {
	const auto itor = Entries.find(steamId64);
	return itor == Entries.end() ? 0 : itor->second.generation;
}

}
