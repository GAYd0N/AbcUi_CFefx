#pragma once

// Animated WebP backend (libwebp demux + animation decoder - the static
// LibWebP target the pinned FreeImage submodule builds, so no new runtime
// DLL ships with the plugin).
//
// Metadata comes from WebPDemux, so the per-frame durations are known without
// decoding any pixel; frames are composited by WebPAnimDecoder in MODE_RGBA,
// which yields the full canvas per frame in straight-alpha top-down RGBA - the
// layout the texture channel uploads. MODE_RGBA also means the decoder applies
// the container's blend/dispose rectangles, so a well-formed animated WebP
// shows exactly what a browser shows.

#include "AnimatedImage.h"

namespace rmlui::animated::detail {

const Backend &webpBackend();

} // namespace rmlui::animated::detail
