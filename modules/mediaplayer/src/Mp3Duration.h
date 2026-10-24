#pragma once

// Mp3Duration - frame-walking duration estimate for MPEG audio streams.
//
// The media player delegates playback to the engine's own mp3 stream (AIL):
// neither the engine nor the module ABI exposes the stream position, so the
// page derives elapsed time from its own clock and needs the track length.
// This is metadata parsing only (frame headers plus the Xing/Info VBR
// summary); no audio is decoded, resampled or stored.

#include <cstddef>
#include <cstdint>

namespace mediaplayer {

// Length of the MPEG audio stream in seconds, or 0 when no usable frame was
// found (unknown container, corrupt data, free-format bitrate).
double mp3DurationSeconds(const std::uint8_t *data, std::size_t size);

} // namespace mediaplayer
