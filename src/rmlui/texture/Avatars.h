#pragma once

// data::avatars - scoreboard avatar textures (steamapi facade consumer).
// "avatar://<steamid64>" URIs resolve here from
// AbcRenderInterface::LoadTexture: missing avatars return an error; ready ones
// upload through the VGUI surface texture channel. The scoreboard refresh
// rewrites ready rows' src (cache-bust suffix) so Rml re-resolves once the RGBA
// landed.
//
// Animated avatars are a second, compressed cache: the APNG bytes from Steam
// arrive through the profile-item + ISteamHTTP path, are kept under the same
// 256-id bound with an 8 MiB byte budget, and are mirrored to
// "<mod>/abc/abcui/avatarcache/<steamid64>.apng" (versioned header, atomic
// replace) so reopening the scoreboard does not refetch. "<animimg>" resolves
// "avataranim://<steamid64>?gen=<n>" through animatedBytes(); the static
// "avatar://" path is unchanged.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace avatars {

int status(std::uint64_t steamId64, int &generationOut);
void shutdown();
// Main-thread service pump; independent of scoreboard visibility.
void onFrame();

// Texture id for a URI load (zero when unavailable);
// width/height receive the pixel size. `triggerFetch` starts the steamapi
// download attempt when the id is not ready yet.
int textureFor(std::uint64_t steamId64, int &width, int &height,
               bool triggerFetch);

// Cache-bust generation for "avatar://<id>?gen=<n>": bumped once the RGBA
// for this id uploaded, so the scoreboard rewrite makes Rml re-resolve the
// (new) source. 0 = no uploaded avatar.
int generation(std::uint64_t steamId64);

// Frame-pump poll for the steamapi download (old CAvatarImage::Paint retry):
// runs the same 1s-throttled fetch attempt as textureFor. True when THIS call
// uploaded the RGBA (the caller must refresh its rows - Rml caches textures
// per source string, failures included, so a landing avatar only reaches the
// screen through a new ?gen URL on a rebuilt row).
bool touch(std::uint64_t steamId64);

// Animated-avatar probe/cache. OK when the APNG bytes are cached (generation
// receives the animation cache-bust value); PENDING while Steam or the
// download is still working; NOT_FOUND when this user has no animated avatar
// (or the runtime cannot serve one); UNAVAILABLE without Steam. Only called
// while the scoreboard rebuilds a visible row, so a closed board starts no
// profile/network work.
int animatedStatus(std::uint64_t steamId64, int &generationOut);

// Copies the cached APNG bytes for an "avataranim://" element source. Does
// not probe: a missing entry means the source cannot open (the row falls back
// on the next rebuild).
bool animatedBytes(std::uint64_t steamId64, std::vector<std::byte> &out);

} // namespace avatars
