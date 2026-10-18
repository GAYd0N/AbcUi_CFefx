#pragma once

// services::texture::DynamicTexture - a live RGBA texture fed by decoded
// pixels and published to Rml documents as an external texture. The pattern is
// the batch-D radar one (registerExternalTexture + a stable URI whose query
// suffix only bumps when the GPU texture is recreated), so an element that
// keeps the same `src` string costs RmlUi zero texture work per frame while
// the pixels keep changing underneath.
//
// Extracted from services::video::VideoTexture when the spray maker needed the
// same plumbing: one implementation, one owner per instance.

#include <string>

namespace services::texture {

class DynamicTexture {
public:
  // `prefix` must be process-unique per instance (it keys the Rml external
  // texture registry).
  explicit DynamicTexture(std::string prefix);
  ~DynamicTexture();
  DynamicTexture(const DynamicTexture &) = delete;
  DynamicTexture &operator=(const DynamicTexture &) = delete;

  // Uploads one frame (top-down tightly packed RGBA8). (Re)creates the GPU
  // texture when the size changed. Returns false when the VGUI surface
  // texture channel is unavailable.
  bool upload(const unsigned char *rgba, int width, int height);

  // Rml image source for the current texture; empty before the first upload.
  const std::string &uri() const {
    return Uri;
  }
  bool valid() const {
    return TextureId != 0;
  }
  // Raw texture id for consumers that draw the frame themselves.
  int textureId() const {
    return TextureId;
  }

  void release();

private:
  int TextureId = 0;
  int Width = 0;
  int Height = 0;
  unsigned int Generation = 0;
  std::string Prefix;
  std::string Uri;
};

} // namespace services::texture
