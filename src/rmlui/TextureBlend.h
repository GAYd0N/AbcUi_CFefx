#pragma once

namespace rmlui {

// Draw program an RGBA texture is rendered through. Most Rml content is
// straight alpha; the old GoldSrc HUD sprites were kRenderTransAdd, which the
// renderer expresses as SRC_ALPHA / ONE (see AbcRenderInterface), and RmlUi's
// own font atlases arrive premultiplied.
//
// Owned here rather than in AbcRenderInterface.h so the texture owners
// (services::texture::DynamicTexture, the animated image element) can name a
// blend mode without pulling the whole render interface into their headers.
enum class TextureBlend {
  StraightAlpha,
  PremultipliedAlpha,
  Additive,
};

} // namespace rmlui
