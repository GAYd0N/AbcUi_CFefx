// rmlui::video::ElementVideo implementation - see ElementVideo.h.
#include "ElementVideo.h"
#include "../../services/video/AsyncDecoder.h"
#include "../../services/video/Options.h"
#include "VideoTexture.h"

#include <windows.h>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>

#include <abc/Console.h>
#include <abc/Paths.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace rmlui::video {

namespace {

// Upper bound of the decoded frame's long edge. The backdrop is blurred and the
// menu previews are small, so decoding beyond this buys nothing but upload
// traffic (a full RGBA frame is re-uploaded whenever the movie advances).
constexpr int kMaxDecodeEdge = 1024;
constexpr int kMinDecodeEdge = 16;
// A box change smaller than this keeps the current decode target (dragging a
// window edge must not restart the decoder on every pixel).
constexpr int kResizeHysteresis = 8;

std::vector<ElementVideo *> &elements() {
  static std::vector<ElementVideo *> Elements;
  return Elements;
}

Rml::ElementInstancerGeneric<ElementVideo> Instancer;

#ifndef NDEBUG
// Debug instrument (abc_ui_video_test): decode + upload cost per HUD frame,
// the same avg/max window shape as rmlui::perfSnapshot.
struct PerfWindow {
  int Frames = 0;
  double TotalMs = 0.0;
  double MaxMs = 0.0;
};
PerfWindow Perf;
#endif

} // namespace

ElementVideo::ElementVideo(const Rml::String &Tag)
    : Element(Tag), Config(std::make_unique<::video::Options>()),
      Media(std::make_unique<::video::AsyncDecoder>()),
      Texture(std::make_unique<::video::VideoTexture>()) {
  auto Child = Rml::Factory::InstanceElement(this, "img", "img", {});
  Image = Child.get();
  if (Image) {
    Image->SetProperty("position", "absolute");
    Image->SetProperty("display", "block");
    Image->SetProperty("pointer-events", "none");
    Image->SetProperty("visibility", "hidden");
    AppendChild(std::move(Child));
  }
  elements().push_back(this);
}

const ::video::FrameView &ElementVideo::currentFrame() const {
  return Media->frame();
}

double ElementVideo::currentSeconds() const {
  return Media->lastSeconds();
}

ElementVideo::~ElementVideo() {
  std::erase(elements(), this);
}

void ElementVideo::closeMedia() {
  Media->close();
  Texture->release();
  Hidden = true;
  DecodeSize = {};
  SourceApplied = false;
  for (float &value : Placement)
    value = 0.0f;
  if (Image) {
    Image->SetAttribute("src", "");
    Image->SetProperty("visibility", "hidden");
  }
}

void ElementVideo::reset() {
  closeMedia();
}

void ElementVideo::OnAttributeChange(const Rml::ElementAttributes &Changed) {
  Element::OnAttributeChange(Changed);
  // Reparse from defaults so RemoveAttribute resets instead of retaining the
  // removed value; an invalid known attribute is reported and keeps its own
  // previous value, never a half-parsed state.
  ::video::Options Updated;
  for (const auto &[Name, Value] : GetAttributes()) {
    if (!Updated.set(Name, Value.Get<Rml::String>()))
      abc::ConsoleWarn("[AbcUi video] invalid %s on #%s\n", Name.c_str(),
                       GetId().c_str());
  }
  *Config = std::move(Updated);
}

void ElementVideo::applyImageSource() {
  if (!Image)
    return;
  const std::string &uri = Texture->uri();
  const Rml::String current = Image->GetAttribute<Rml::String>("src", "");
  if (current != uri)
    Image->SetAttribute("src", uri);
  if (Hidden) {
    Image->SetProperty("visibility", "visible");
    Hidden = false;
  }
}

void ElementVideo::applyPlacement(Rml::Vector2i box, float width,
                                  float height) {
  if (!Image)
    return;
  const float left = (static_cast<float>(box.x) - width) * 0.5f;
  const float top = (static_cast<float>(box.y) - height) * 0.5f;
  // Change-gated (05 §8 performance rule): a steady box writes nothing.
  const float values[4] = {width, height, left, top};
  const char *const names[4] = {"width", "height", "left", "top"};
  for (int i = 0; i < 4; i++) {
    if (Placement[i] == values[i])
      continue;
    Placement[i] = values[i];
    char text[32];
    _snprintf_s(text, sizeof(text), _TRUNCATE, "%.2fpx", values[i]);
    Image->SetProperty(names[i], text);
  }
}

// Sizes and places the inner <img> for the configured fit mode; returns the
// displayed pixel size (the decode target is derived from it).
Rml::Vector2i ElementVideo::applyFit(Rml::Vector2i box) {
  const int sourceWidth = Media->sourceWidth();
  const int sourceHeight = Media->sourceHeight();
  if (sourceWidth <= 0 || sourceHeight <= 0 || box.x <= 0 || box.y <= 0)
    return {};
  const float aspect =
      static_cast<float>(sourceWidth) / static_cast<float>(sourceHeight);
  float width = static_cast<float>(box.x);
  float height = static_cast<float>(box.y);
  if (Config->FitMode == ::video::Options::Fit::kCover) {
    height = width / aspect;
    if (height < static_cast<float>(box.y)) {
      height = static_cast<float>(box.y);
      width = height * aspect;
    }
  } else if (Config->FitMode == ::video::Options::Fit::kContain) {
    height = width / aspect;
    if (height > static_cast<float>(box.y)) {
      height = static_cast<float>(box.y);
      width = height * aspect;
    }
  }
  applyPlacement(box, width, height);
  return {static_cast<int>(width + 0.5f), static_cast<int>(height + 0.5f)};
}

bool ElementVideo::openMedia(const char *reason) {
  const std::string path = abc::ModPath(Config->Source.c_str());
  const int width = DecodeSize.x > 0 ? DecodeSize.x : kMaxDecodeEdge;
  const int height = DecodeSize.y > 0 ? DecodeSize.y : kMaxDecodeEdge;
  if (Media->open(path.c_str(), width, height)) {
    SourceApplied = true;
    return true;
  }
  // One warning per distinct failure per element; the document keeps
  // running with the element invisible.
  if (LastError != Media->error()) {
    LastError = Media->error();
    abc::ConsoleWarn("[AbcUi video] %s (%s): %s\n", Config->Source.c_str(),
                     reason, LastError.c_str());
  }
  return false;
}

bool ElementVideo::pump(double frameTime, bool Enabled) {
  if (!Enabled || !Image || !GetContext() || !IsVisible(true)) {
    Media->pause();
    return false;
  }
  if (Config->Source.empty()) {
    if (SourceApplied)
      closeMedia();
    return false;
  }
  if (AppliedSource != Config->Source) {
    closeMedia();
    AppliedSource = Config->Source;
  }
  const auto size = GetBox().GetSize(Rml::BoxArea::Content);
  if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x <= 1.0f ||
      size.y <= 1.0f) {
    Media->pause();
    return false;
  }

  if (!Media->isOpen() && !openMedia("open")) {
    DecodeSize = {};
    return false;
  }

  Media->setLooping(Config->Loop);
  // autoplay=false holds the very first frame: the clock only moves when the
  // document asked for playback.
  const double delta =
      Config->Autoplay ? static_cast<double>(frameTime) * Config->Speed : 0.0;
  const bool advanced = Media->advance(delta);
  if (advanced) {
    const ::video::FrameView &frame = Media->frame();
    if (frame.Pixels &&
        !Texture->upload(frame.Pixels, frame.Width, frame.Height))
      return false;
  }
  if (!Media->error().empty() && LastError != Media->error()) {
    LastError = Media->error();
    abc::ConsoleWarn("[AbcUi video] %s: %s\n", Config->Source.c_str(),
                     LastError.c_str());
  }

  const Rml::Vector2i box{static_cast<int>(size.x + 0.5f),
                          static_cast<int>(size.y + 0.5f)};
  const Rml::Vector2i displayed = applyFit(box);
  if (displayed.x <= 0 || displayed.y <= 0)
    return false;

  // Decode at the displayed size, capped: the frame never needs more detail
  // than the box shows, and a bounded edge keeps the per-frame upload sane.
  const int longEdge = std::max(displayed.x, displayed.y);
  const float quality = longEdge > kMaxDecodeEdge
                            ? static_cast<float>(kMaxDecodeEdge) / longEdge
                            : 1.0f;
  const Rml::Vector2i target{std::clamp(static_cast<int>(displayed.x * quality),
                                        kMinDecodeEdge, kMaxDecodeEdge),
                             std::clamp(static_cast<int>(displayed.y * quality),
                                        kMinDecodeEdge, kMaxDecodeEdge)};
  if (std::abs(DecodeSize.x - target.x) > kResizeHysteresis ||
      std::abs(DecodeSize.y - target.y) > kResizeHysteresis) {
    // Reconfigure on the worker, not in the HUD frame. Tiny layout
    // changes keep the current output target rather than reopening.
    DecodeSize = target;
    if (!openMedia("resize")) {
      DecodeSize = {};
      return false;
    }
  }
  applyImageSource();
  return advanced;
}

void registerElement() {
  Rml::Factory::RegisterElementInstancer("video", &Instancer);
}

bool pumpFrames(double frameTime, bool Enabled) {
#ifndef NDEBUG
  LARGE_INTEGER frequency, start, end;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&start);
#endif
  bool changed = false;
  for (auto *element : elements())
    if (element->pump(frameTime, Enabled))
      changed = true;
#ifndef NDEBUG
  QueryPerformanceCounter(&end);
  const double milliseconds =
      1000.0 * static_cast<double>(end.QuadPart - start.QuadPart) /
      frequency.QuadPart;
  Perf.Frames++;
  Perf.TotalMs += milliseconds;
  Perf.MaxMs = Perf.MaxMs > milliseconds ? Perf.MaxMs : milliseconds;
#endif
  return changed;
}

#ifndef NDEBUG
PerfSnapshot perfSnapshot() {
  PerfSnapshot out;
  out.Frames = Perf.Frames;
  out.AverageMs = Perf.Frames ? Perf.TotalMs / Perf.Frames : 0.0;
  out.MaximumMs = Perf.MaxMs;
  Perf = {};
  return out;
}
#endif

void invalidateAll() {
  for (auto *element : elements())
    element->reset();
}

} // namespace rmlui::video
