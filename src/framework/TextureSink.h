#pragma once

// framework::texture - the RGBA texture channel used by the data layer.
//
// Sprite atlases and avatars need a VGUI surface texture, but the data layer
// must not include the RmlUi/VGUI2 import implementation. This header is the
// narrow seam; src/rmlui/TextureChannel.cpp binds it to VGUI2Extension.
// Every call is safe while the channel is unavailable (create returns 0,
// upload/release are no-ops).

namespace framework::texture {

bool available();
int create();
void upload(int id, const unsigned char *rgba, int width, int height);
void release(int id);

} // namespace framework::texture
