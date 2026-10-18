#pragma once

// services::video::VideoTexture - the menu video's live frame published as an
// Rml external texture. A thin owner of services::texture::DynamicTexture with
// the video's own registration prefix, so video and spray share one
// implementation (the plumbing moved there).

#include "../texture/DynamicTexture.h"

#include <string>

namespace video {

class VideoTexture {
public:
  VideoTexture();
  ~VideoTexture();
  VideoTexture(const VideoTexture &) = delete;
  VideoTexture &operator=(const VideoTexture &) = delete;

  // Uploads one frame (top-down tightly packed RGBA8). (Re)creates the GPU
  // texture when the size changed. Returns false when the VGUI surface
  // texture channel is unavailable.
  bool upload(const unsigned char *rgba, int width, int height) {
    return Texture.upload(rgba, width, height);
  }

  // Rml image source for the current texture; empty before the first upload.
  const std::string &uri() const {
    return Texture.uri();
  }
  bool valid() const {
    return Texture.valid();
  }
  // Raw texture id for consumers that draw the frame themselves (the menu
  // backdrop blurs it instead of showing it directly).
  int textureId() const {
    return Texture.textureId();
  }

  void release() {
    Texture.release();
  }

private:
  services::texture::DynamicTexture Texture;
};

} // namespace video
