#pragma once

// Animated PNG (APNG) backend for the generic <animimg> element.
//
// Steam animated avatars are APNG files; this backend is also the first
// animated-image format the scoreboard reaches through AnimatedImage's
// backend table. It walks the container chunks itself (IHDR, acTL, fcTL,
// fdAT, IDAT, PLTE/tRNS) and hands each frame's compressed pixel stream to
// framework::ImageLoader (FreeImage) by assembling a valid single-frame PNG.
// FreeImage therefore stays the only PNG decoder in the plugin: no second
// zlib/libpng dependency and no RmlUi submodule change.
//
// The APNG composite model is implemented here, not delegated: frame rects,
// dispose (none/background/previous) and blend (source/over) are applied on a
// straight-alpha RGBA canvas, which is exactly the FrameView layout the
// texture channel uploads. APNG frame payloads are copied out of the resident
// file bytes at open time; decode is per frame and sequential, like the WebP
// backend.

#include "AnimatedImage.h"

#include <span>

namespace rmlui::animated::detail {

const Backend &apngBackend();

// True when the bytes are a PNG carrying an acTL chunk before IDAT (i.e. an
// APNG). The avatar cache uses this to reject URLs that return a static PNG,
// HTML error pages or anything else PhotoImage could decode.
bool isAnimatedPng(std::span<const std::byte> bytes);

} // namespace rmlui::animated::detail
