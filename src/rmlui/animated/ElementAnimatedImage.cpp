// <animimg> implementation - see ElementAnimatedImage.h.
#include "ElementAnimatedImage.h"
#include "modsys/ModuleVfs.h"
#include <RmlUi/Core/ElementDocument.h>

#include "../texture/Avatars.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/SystemInterface.h>

#include <abc/Console.h>
#include <abc/Parse.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AnimatedImage.h"
#include "../texture/DynamicTexture.h"

namespace rmlui::animated {

namespace {

// 10 fps is the old CSPRImage framerate and the fallback for containers that
// carry no per-frame duration at all.
constexpr int kDefaultFrameMs = 100;
constexpr float kMinSpeed = 0.05f;
constexpr float kMaxSpeed = 8.0f;
// A HUD hitch (map load, alt-tab) must not fast-forward the whole sequence.
constexpr double kMaxStepSeconds = 0.25;

std::vector<ElementAnimatedImage *> &elements() {
  static std::vector<ElementAnimatedImage *> Elements;
  return Elements;
}

// Playback origin per animated-avatar source. The scoreboard rebuilds its
// table every 0.5 s (live ping/health), which recreates every <animimg>; a
// fresh element would otherwise restart the APNG at frame 0 four times per
// second. The origin makes a replacement element resume at the position the
// previous one had reached. The map is bounded; the source's ?gen suffix
// changes when the bytes change, so a new animation correctly starts at 0.
std::unordered_map<std::string, double> &avatarClocks() {
  static std::unordered_map<std::string, double> Clocks;
  return Clocks;
}

constexpr std::string_view kAvatarAnimPrefix = "avataranim://";

bool parseAvatarAnimSource(const std::string &source, std::uint64_t &id) {
  if (!source.starts_with(kAvatarAnimPrefix))
    return false;
  const std::size_t begin = kAvatarAnimPrefix.size();
  std::size_t end = begin;
  while (end < source.size() && source[end] >= '0' && source[end] <= '9')
    ++end;
  if (end == begin || end - begin > 20)
    return false;
  std::uint64_t value = 0;
  for (std::size_t i = begin; i < end; ++i) {
    const std::uint64_t digit = static_cast<std::uint64_t>(source[i] - '0');
    if (value > (UINT64_MAX - digit) / 10)
      return false;
    value = value * 10 + digit;
  }
  id = value;
  return id != 0;
}

Rml::ElementInstancerGeneric<ElementAnimatedImage> Instancer;

// Process-unique registration prefix, so two elements can never share one
// external texture registration.
std::string makePrefix() {
  static unsigned long long Counter = 0;
  return "animtex://" + std::to_string(++Counter) + "/";
}

bool parseBool(const Rml::String &text, bool &value) {
  if (text == "true" || text == "1") {
    value = true;
    return true;
  }
  if (text == "false" || text == "0") {
    value = false;
    return true;
  }
  return false;
}

bool parseFloat(const Rml::String &text, float &value) {
  if (text.empty())
    return false;
  const auto parsed = abc::ParseFloat(text);
  if (!parsed || !std::isfinite(*parsed))
    return false;
  value = *parsed;
  return true;
}

bool parseBlend(const Rml::String &text, TextureBlend &blend) {
  if (text == "alpha" || text == "straight") {
    blend = TextureBlend::StraightAlpha;
    return true;
  }
  if (text == "add" || text == "additive") {
    blend = TextureBlend::Additive;
    return true;
  }
  return false;
}

} // namespace

ElementAnimatedImage::ElementAnimatedImage(const Rml::String &Tag)
    : Element(Tag) {
  auto Child = Rml::Factory::InstanceElement(this, "img", "img", {});
  FrameImage = Child.get();
  if (FrameImage) {
    FrameImage->SetProperty("position", "absolute");
    FrameImage->SetProperty("left", "0px");
    FrameImage->SetProperty("top", "0px");
    FrameImage->SetProperty("width", "100%");
    FrameImage->SetProperty("height", "100%");
    FrameImage->SetProperty("pointer-events", "none");
    // An <img> whose texture cannot be resolved draws as a filled box, so the
    // child stays hidden until the first frame uploaded.
    FrameImage->SetProperty("visibility", "hidden");
    AppendChild(std::move(Child));
  }
  elements().push_back(this);
}

ElementAnimatedImage::~ElementAnimatedImage() {
  std::erase(elements(), this);
}

int ElementAnimatedImage::frameCount() const {
  return Image ? Image->FrameCount() : 0;
}

void ElementAnimatedImage::OnAttributeChange(
    const Rml::ElementAttributes &Changed) {
  Element::OnAttributeChange(Changed);
  // Reparse from the defaults so RemoveAttribute resets instead of retaining
  // the removed value; an invalid known value is reported and keeps its
  // previous value (the ElementVideo/ElementModel attribute contract).
  std::string source;
  bool loop = Loop;
  bool autoplay = Autoplay;
  float speed = Speed;
  TextureBlend blend = Blend;
  for (const auto &[Name, Value] : GetAttributes()) {
    const Rml::String text = Value.Get<Rml::String>();
    if (Name == "src") {
      source = text;
      continue;
    }
    bool valid = true;
    if (Name == "loop")
      valid = parseBool(text, loop);
    else if (Name == "autoplay")
      valid = parseBool(text, autoplay);
    else if (Name == "speed") {
      float parsed = 0.0f;
      valid = parseFloat(text, parsed) && parsed >= kMinSpeed &&
              parsed <= kMaxSpeed;
      if (valid)
        speed = parsed;
    } else if (Name == "blend")
      valid = parseBlend(text, blend);
    else
      continue; // id, class, style, ... - not this element's contract
    if (!valid)
      abc::ConsoleWarn("[AbcUi animimg] %s: invalid %s='%s'\n", GetId().c_str(),
                       Name.c_str(), text.c_str());
  }

  const bool reopen = source != Source || blend != Blend;
  Source = std::move(source);
  Loop = loop;
  Autoplay = autoplay;
  Speed = speed;
  Blend = blend;
  if (reopen)
    closeSource();
}

void ElementAnimatedImage::closeSource() {
  Texture.reset();
  Image.reset();
  AppliedSource.clear();
  TextureReady = false;
  Playing = false;
  FrameIndex = -1;
  FrameClock = 0.0;
  if (FrameImage) {
    FrameImage->SetAttribute("src", "");
    FrameImage->SetProperty("visibility", "hidden");
  }
}

bool ElementAnimatedImage::ensureOpen() {
  if (Image && AppliedSource == Source)
    return true;
  closeSource();
  if (Source.empty())
    return false;

  std::uint64_t avatarId = 0;
  const bool avatarSource = parseAvatarAnimSource(Source, avatarId);
  std::string error, resolved;
  std::unique_ptr<AnimatedImage> image;
  if (avatarSource) {
    std::vector<std::byte> bytes;
    if (avatars::animatedBytes(avatarId, bytes)) {
      image = openMemory(std::move(bytes), Source, error);
    } else {
      error = "animated avatar not cached";
    }
  } else {
    auto *document = GetOwnerDocument();
    if (document && framework::ModuleVfs::resolve(document->GetSourceURL(),
                                                  Source, resolved))
      image = openFile(resolved, error);
    else
      error = "cannot resolve " + Source;
  }
  if (!image) {
    // Remember the failure so a missing asset warns once, not every frame; a
    // different source (or invalidate()) retries.
    AppliedSource = Source;
    if (WarnedSource != Source) {
      WarnedSource = Source;
      if (!avatarSource)
        abc::ConsoleWarn("[AbcUi animimg] %s\n", error.c_str());
    }
    // A cached-avatar miss must not leave a hole in the scoreboard row: draw
    // the ordinary static avatar until the next table rebuild tries again.
    if (avatarSource && FrameImage) {
      const std::string fallback =
          "avatar://" + std::to_string(avatarId) +
          "?gen=" + std::to_string(avatars::generation(avatarId));
      FrameImage->SetAttribute("src", fallback);
      FrameImage->SetProperty("visibility", "visible");
      TextureReady = true;
    }
    return false;
  }

  Image = std::move(image);
  AppliedSource = Source;
  FrameIndex = -1;
  FrameClock = 0.0;
  Texture =
      std::make_unique<services::texture::DynamicTexture>(makePrefix(), Blend);
  Playing = Autoplay;
  // The intrinsic size becomes known with the animation: lay out again.
  DirtyLayout();

  if (avatarSource) {
    // A replacement row resumes where the previous element left the source;
    // the first element of a source simply starts at frame 0.
    const double now = Rml::GetSystemInterface()->GetElapsedTime();
    auto &clocks = avatarClocks();
    if (clocks.size() > 512)
      clocks.clear();
    const auto [itor, inserted] = clocks.try_emplace(Source, now);
    (void)inserted;
    seekToClock(itor->second, now);
    return TextureReady;
  }

  // Show frame 0 immediately (a controller may have made the element visible
  // in this same HUD frame).
  FrameView frame;
  if (!Image->Rewind() || !Image->Read(frame))
    return false;
  return uploadFrame(frame);
}

void ElementAnimatedImage::seekToClock(double startSeconds, double now) {
  if (!Image || Image->FrameCount() <= 0)
    return;
  int total = 0;
  for (int i = 0; i < Image->FrameCount(); ++i) {
    const int duration = Image->DurationMs(i);
    total += duration > 0 ? duration : kDefaultFrameMs;
  }
  if (total <= 0)
    return;
  double elapsedMs = std::max(0.0, now - startSeconds) * 1000.0;
  double positionMs = std::fmod(elapsedMs, static_cast<double>(total));
  int target = 0;
  for (; target < Image->FrameCount(); ++target) {
    const int duration = Image->DurationMs(target);
    const int frameMs = duration > 0 ? duration : kDefaultFrameMs;
    if (positionMs < frameMs)
      break;
    positionMs -= frameMs;
  }
  if (target >= Image->FrameCount()) {
    target = Image->FrameCount() - 1;
    positionMs = 0.0;
  }
  if (!Image->Rewind())
    return;
  FrameView frame;
  for (int index = 0; index <= target; ++index) {
    if (!Image->Read(frame))
      return;
  }
  if (uploadFrame(frame))
    FrameClock = positionMs / 1000.0;
}

bool ElementAnimatedImage::uploadFrame(const FrameView &frame) {
  if (!Texture || !frame.Pixels || frame.Width <= 0 || frame.Height <= 0)
    return false;
  if (!Texture->upload(frame.Pixels, frame.Width, frame.Height))
    return false;
  FrameIndex = frame.Index;
  FrameDurationMs = frame.DurationMs > 0 ? frame.DurationMs : kDefaultFrameMs;
  const std::string &uri = Texture->uri();
  if (FrameImage) {
    // Change-gated: a same-size frame reuses the texture id and the URI, so
    // steady playback writes no DOM at all.
    if (FrameImage->GetAttribute<Rml::String>("src", "") != uri)
      FrameImage->SetAttribute("src", uri);
    if (!TextureReady) {
      TextureReady = true;
      FrameImage->SetProperty("visibility", "visible");
    }
  }
  return true;
}

bool ElementAnimatedImage::advanceFrame() {
  if (!Image)
    return false;
  FrameView frame;
  if (Image->Read(frame))
    return uploadFrame(frame);
  if (!Loop) {
    Playing = false;
    return false;
  }
  if (!Image->Rewind() || !Image->Read(frame)) {
    Playing = false;
    return false;
  }
  return uploadFrame(frame);
}

bool ElementAnimatedImage::Restart() {
  if (!ensureOpen())
    return false;
  FrameView frame;
  if (!Image->Rewind() || !Image->Read(frame) || !uploadFrame(frame))
    return false;
  FrameClock = 0.0;
  Playing = true;
  return true;
}

void ElementAnimatedImage::setPlaying(bool playing) {
  Playing = playing;
}

void ElementAnimatedImage::invalidate() {
  closeSource();
  LastTime = 0.0;
}

void ElementAnimatedImage::OnUpdate() {
  const double now = Rml::GetSystemInterface()->GetElapsedTime();
  const double delta = LastTime > 0.0 ? now - LastTime : 0.0;
  LastTime = now;

  if (!FrameImage)
    return;
  if (!Image && !Source.empty() && AppliedSource != Source)
    ensureOpen();
  if (!Image || !Playing)
    return;
  // Hidden documents (display:none panels, closed menus) must not burn decode
  // time; visibility gates the clock, so playback resumes seamlessly.
  if (!IsVisible(true))
    return;

  FrameClock += std::min(delta, kMaxStepSeconds) * static_cast<double>(Speed);
  double frameSeconds = FrameDurationMs / 1000.0;
  while (Playing && FrameClock >= frameSeconds) {
    FrameClock -= frameSeconds;
    if (!advanceFrame()) {
      FrameClock = 0.0;
      break;
    }
    frameSeconds = FrameDurationMs / 1000.0;
  }
}

bool ElementAnimatedImage::GetIntrinsicDimensions(Rml::Vector2f &dimensions,
                                                  float &ratio) {
  if (!Image || Image->Width() <= 0 || Image->Height() <= 0)
    return false;
  // dp-scaled like <img>: the intrinsic size is authored in pixels at the
  // document's design density.
  float scale = 1.0f;
  if (Rml::Context *context = GetContext())
    scale = context->GetDensityIndependentPixelRatio();
  dimensions = Rml::Vector2f(static_cast<float>(Image->Width()) * scale,
                             static_cast<float>(Image->Height()) * scale);
  ratio =
      static_cast<float>(Image->Width()) / static_cast<float>(Image->Height());
  return true;
}

void registerElement() {
  Rml::Factory::RegisterElementInstancer("animimg", &Instancer);
}

void invalidateAll() {
  for (auto *element : elements())
    element->invalidate();
}

} // namespace rmlui::animated

RMLUI_RTTI_Define(rmlui::animated::ElementAnimatedImage)
