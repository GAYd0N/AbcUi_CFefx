#pragma once

// data::sprites - hud.txt sprite registry + GoldSrc .spr decoder feeding
// Rml textures. Ported from ABCEnchance src/core/resource/spriteresource.*
// (hud.txt runs through SPR_GetList; atlas bytes resolve through the engine
// IFileSystem, so all mounted *_downloads/*_addon paths participate);
// the pixel half is new - the old VGUI controls drew engine HSPRITE handles,
// which Rml cannot consume. Decode rules ported from SPRView.Net (Core/
// FrameDecoder.cs, same author/maintainer as this migration):
//
//   header  40 bytes: 'IDSP', version 2, type, format, boundRadius,
//                     maxFrameWidth/Height, numFrames, beamLength, sync
//   palette int16 count + count * RGB
//   frames  per frame: group/originX/originY/width/height ints + indexed
//           pixels; malformed trailing entries end the table (tolerated,
//           files in the wild carry them)
//   transparency by format: IndexAlpha(2) index==alpha; Additive(1) black
//           and the palette-255 key colour are empty; Normal/AlphaTest only
//           the key colour (full 256-entry palettes only).
//
// Rml elements reference sprites through URIs intercepted in
// AbcRenderInterface::LoadTexture (never the file interface):
//   spr://<name>       -> a hud.txt entry (ItemStack pickups etc.)
//   spricon://<atlas>/<l>,<t>,<r>,<b>
//                      -> a crop of a sprites/<atlas> frame (weapon icons)
//   spralpha://<atlas>/<l>,<t>,<r>,<b>
//                      -> same crop, drawn straight-alpha (old
//                         kRenderTransTexture sprites: weapon crosshairs)
//   sprframe://<path>|<frame>
//                      -> frame <frame> of a standalone .spr file
//                         (batch B: Effect water splash, 256x512 additive)
// Textures are VGUI2 surface texture ids (the IMetaRenderer draw namespace),
// created lazily and cached per URI. Atlas frames decode once.
//
// HUD sprites are Additive format; we render them straight-alpha with the
// SPRView "black is empty" rule instead of true additive blending (the
// renderer's blend set has no ONE/ONE_MINUS_SRC_ALPHA, D-13). Over the dark
// HUD backgrounds this is visually equivalent; noted as a known deviation.
// AbcRenderInterface marks the additive-blend program per URI prefix, which
// restores the old kRenderTransAdd draw for the icons that need it.

#include <string>
#include <unordered_map>

#include <wrect.h>

namespace sprites {

// Parses sprites/hud.txt through the engine (first call only). HUD_Init-safe;
// call again on HUD_VidInit like the old gSpriteRes.VidInit().
void vidInit();
// Releases every cached texture + decoded atlas.
void shutdown();

// hud.txt lookup: fills the atlas file ("640hud7.spr") and sprite rect.
bool findHudSprite(const char* name, std::string& atlasFile, wrect_t& rect);

// Decodes <sprites/atlasFile> (lazily, once) and returns a texture cropped
// to <rect>. 0 when the atlas or the texture channel is unavailable. The
// straightAlpha flag keys the cache: the same crop under both blend flavors
// must not share a texture id (the additive mark rides the id).
int croppedTexture(const std::string& atlasFile, const wrect_t& rect, bool straightAlpha = false);

// URI front-end for AbcRenderInterface::LoadTexture. Accepts "spr://name",
// "spricon://atlas/l,t,r,b", "spralpha://atlas/l,t,r,b" and
// "sprframe://path|frame". Returns the texture id and fills width/height with
// the crop/frame size; 0 on unknown URIs.
int textureForUri(const std::string& uri, int& width, int& height);

// Number of frames in a standalone .spr file (0 when it cannot be read) - the
// Effect panel drives its animation loop with it.
int standaloneFrameCount(const std::string& relativePath);

}
