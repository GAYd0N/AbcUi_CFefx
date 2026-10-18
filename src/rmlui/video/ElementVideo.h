#pragma once

// rmlui::video::ElementVideo - the <video> Rml element.
//
// Same shape as <model>: one element owns one media instance and an internal
// <img> child draws it, so clipping, opacity, transforms and z-order come from
// the ordinary Rml renderer. The element never touches geometry itself; it
// answers "does this element need a new frame" and the module pumps all live
// elements once per HUD frame (rmlui::video::pumpFrames), outside Rml's own
// geometry pass.
//
// Attribute contract (services::video::Options):
//   src       mod-relative or absolute video file path
//   loop      true|false (default true)
//   autoplay  true|false (default true)
//   speed     0.05 .. 8 playback multiplier (default 1)
//   fit       cover|contain|fill (default cover)
//   muted     accepted, inert: audio is never decoded

#include <RmlUi/Core/Element.h>

#include <memory>

namespace Rml {
class ElementDocument;
}

namespace video {
struct FrameView;
struct Options;
class AsyncDecoder;
class VideoTexture;
} // namespace video

namespace rmlui::video {

class ElementVideo : public Rml::Element {
public:
  explicit ElementVideo(const Rml::String &Tag);
  ~ElementVideo() override;

  // Decodes/uploads for the current HUD frame. Returns true when the element
  // changed something that needs an Rml update pass afterwards.
  bool pump(double frameTime, bool Enabled);

  // Drops the decoder and the texture (vid restart): the next pump re-opens
  // at the new box size. Safe on a hidden element.
  void reset();

  // Diagnostics (Debug harness, abc_ui_video_test): what the element shows.
  const ::video::FrameView &currentFrame() const;
  double currentSeconds() const;

protected:
  void OnAttributeChange(const Rml::ElementAttributes &Changed) override;

private:
  // Element box -> layout of the inner <img>: cover/contain/fill placement in
  // original pixels. Returns the pixel size the decoder should produce.
  Rml::Vector2i applyFit(Rml::Vector2i box);
  void applyPlacement(Rml::Vector2i box, float width, float height);
  void applyImageSource();
  void closeMedia();
  bool openMedia(const char *reason);

  // note: qualified as ::video:: - the element namespace is rmlui::video and
  // would otherwise shadow the services namespace.
  std::unique_ptr<::video::Options> Config;
  std::unique_ptr<::video::AsyncDecoder> Media;
  std::unique_ptr<::video::VideoTexture> Texture;
  Rml::Element *Image = nullptr;
  bool SourceApplied = false;
  bool Hidden = true;
  // Output pixels of the current decode target; a box change re-opens the
  // media at the new size (cheap: the decoder only holds the file open).
  Rml::Vector2i DecodeSize{};
  std::string AppliedSource;
  std::string LastError;
  // Last applied inline placement values, so a steady box writes no DOM.
  float Placement[4] = {-1.0f, -1.0f, 0.0f, 0.0f};
};

void registerElement();
// One decode/upload step per live element; called from the module's HUD_Frame
// beat (never inside a geometry pass). Returns true when any element changed.
bool pumpFrames(double frameTime, bool Enabled = true);
// Releases decoders and textures (vid restart / shutdown): the next pump
// re-opens at the new box size.
void invalidateAll();

#ifndef NDEBUG
// Debug instrument (abc_ui_video_test): decode + upload cost per HUD frame,
// the same avg/max shape as rmlui::perfSnapshot. Reading resets the window.
struct PerfSnapshot {
  int Frames = 0;
  double AverageMs = 0.0;
  double MaximumMs = 0.0;
};
PerfSnapshot perfSnapshot();
#endif

} // namespace rmlui::video
