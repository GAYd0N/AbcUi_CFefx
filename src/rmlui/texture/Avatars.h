#pragma once

// data::avatars - scoreboard avatar textures (steamapi facade consumer).
// "avatar://<steamid64>" URIs resolve here from
// AbcRenderInterface::LoadTexture: not-yet-downloaded avatars get the bundled
// default and a background fetch kicks off; ready ones upload through the VGUI
// surface texture channel. The scoreboard refresh rewrites ready rows' src
// (cache-bust suffix) so Rml re-resolves once the RGBA landed.

#include <cstdint>

namespace avatars {

void init();
void shutdown();
// Main-thread service pump; independent of scoreboard visibility.
void onFrame();

// Texture id for a URI load (never 0 - falls back to the default avatar);
// width/height receive the pixel size. `triggerFetch` starts the steamapi
// download attempt when the id is not ready yet.
int textureFor(std::uint64_t steamId64, int &width, int &height,
               bool triggerFetch);

// Cache-bust generation for "avatar://<id>?gen=<n>": bumped once the RGBA
// for this id uploaded, so the scoreboard rewrite makes Rml re-resolve the
// (new) source. 0 = still showing the bundled default.
int generation(std::uint64_t steamId64);

// Frame-pump poll for the steamapi download (old CAvatarImage::Paint retry):
// runs the same 1s-throttled fetch attempt as textureFor. True when THIS call
// uploaded the RGBA (the caller must refresh its rows - Rml caches textures
// per source string, failures included, so a landing avatar only reaches the
// screen through a new ?gen URL on a rebuilt row).
bool touch(std::uint64_t steamId64);

} // namespace avatars
