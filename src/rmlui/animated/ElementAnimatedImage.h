#pragma once

// <animimg> - the generic animated image element.
//
// One element owns one animated image (rmlui::animated::AnimatedImage: WebP
// today, APNG for Steam animated avatars later) and an internal <img> child
// draws the current frame, so clipping, opacity, transforms and z-order come
// from the ordinary Rml renderer. Frames decode sequentially into one
// DynamicTexture per element (the VGUI2 surface texture channel): a steady
// playback rewrites no DOM and allocates no texture per frame, and the
// per-element texture is released with the element.
//
// The element is document-agnostic: it animates wherever it is used (HUD
// overlay today, scoreboard avatar later) and only decodes while it is
// visible, so a hidden document costs nothing.
//
// Attribute contract:
//   src       mod-relative image path, e.g.
//             abc/abcui/hud/img/water/face_rainleft.webp
//   loop      true|false (default true): restart at the end of the sequence
//   autoplay  true|false (default true): start playing once the source loads
//   speed     0.05 .. 8 playback multiplier (default 1)
//   blend     alpha|add (default alpha): draw program; `add` reproduces the
//             old GoldSrc kRenderTransAdd sprite draw the HUD sprites use
//
// The frame is drawn by an internal <img>, so opacity is inherited from the
// element (a controller's per-frame fade works unchanged) and a tint is
// `animimg img { image-color: ... }` in the page stylesheet.
//
// Playback follows the container's per-frame duration; a container that
// declares none falls back to kDefaultFrameMs (10 fps, the old sprite rate).
// Controllers restart a one-shot sequence through Restart().

#include <RmlUi/Core/Element.h>

#include <memory>
#include <string>

#include "../TextureBlend.h"

namespace services::texture {
class DynamicTexture;
}

namespace rmlui::animated {

struct FrameView;
class AnimatedImage;

class ElementAnimatedImage : public Rml::Element {
public:
  RMLUI_RTTI_DeclareWithParent(ElementAnimatedImage, Rml::Element)

  explicit ElementAnimatedImage(const Rml::String &Tag);
  ~ElementAnimatedImage() override;

  // Back to frame 0, playing, texture fed - the water splash's OUT_WATER edge.
  // Opens the authored source when it is not open yet. False when the source
  // cannot be loaded (a warning was emitted once per source).
  bool Restart();
  void setPlaying(bool playing);
  bool isPlaying() const {
    return Playing;
  }
  int frameIndex() const {
    return FrameIndex;
  }
  int frameCount() const;

  // Renderer/video-mode reset: drops the decoders and the texture so stale
  // VGUI2 texture ids cannot be drawn; the next update reopens the source at
  // frame 0 (the same contract as <video>).
  void invalidate();

protected:
  void OnUpdate() override;
  void OnAttributeChange(const Rml::ElementAttributes &Changed) override;
  bool GetIntrinsicDimensions(Rml::Vector2f &dimensions, float &ratio) override;

private:
  // Opens the authored source when needed; true when an animation is open.
  bool ensureOpen();
  // Drops decoder + texture; the next ensureOpen() starts over.
  void closeSource();
  // Decodes the next frame (wrapping when `loop` allows).
  bool advanceFrame();
  bool uploadFrame(const FrameView &frame);

  std::unique_ptr<AnimatedImage> Image;
  std::unique_ptr<services::texture::DynamicTexture> Texture;
  Rml::Element *FrameImage = nullptr;

  std::string Source;
  bool Loop = true;
  bool Autoplay = true;
  float Speed = 1.0f;
  TextureBlend Blend = TextureBlend::StraightAlpha;

  // The source the open animation came from; a failed source is remembered
  // here too so a missing file warns once, not once per frame.
  std::string AppliedSource;
  std::string WarnedSource;
  bool TextureReady = false;
  bool Playing = false;
  int FrameIndex = -1;
  int FrameDurationMs = 0;
  // Seconds accumulated toward the current frame's duration.
  double FrameClock = 0.0;
  // Wall clock of the previous update (0 = first update after creation).
  double LastTime = 0.0;
};

void registerElement();

// Renderer/video-mode reset for every live element (<video>'s invalidateAll).
void invalidateAll();

} // namespace rmlui::animated
