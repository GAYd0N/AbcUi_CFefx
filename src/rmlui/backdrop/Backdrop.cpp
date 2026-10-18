// services::menubackdrop::Backdrop implementation - see Backdrop.h.
#include <windows.h>

#include "Backdrop.h"

#include <metahook.h>

#include <abc/Console.h>
#include <abc/Paths.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "../AbcRenderInterface.h"
#include "../RmlUiModule.h"
#include "../../render/Render.h"
#include "../../render/RenderCommon.h"
#include "../../services/GameUiBridge.h"
#include "../../services/video/AsyncDecoder.h"
#include "../video/VideoTexture.h"

namespace menubackdrop {

namespace {

// Downscale factor of the capture target. A blurred backdrop carries no fine
// detail, and a quarter-resolution chain keeps the capture + passes cheap
// enough to run every frame (1920x1080 -> 480x270).
constexpr int kDownscale = 4;
constexpr int kMinEdge = 8;
constexpr int kMaxRounds = 4;
constexpr const char *kTexturePrefix = "backdroptex://";
constexpr const char *kSeenWarnTag = "[AbcUi backdrop]";

// Process-wide monotonic generation: every (re)created backdrop texture takes
// a fresh value, so a stale Rml texture record can never be reused for a
// different GPU texture after a resize or a vid restart.
unsigned long long NextGeneration = 0;

} // namespace

struct Backdrop::State {
  Mode Requested = Mode::kAuto;
  Mode Effective = Mode::kSolid;
  std::string VideoSource = kDefaultVideoSource;
  int Rounds = 2;

  FBO_Container_t Down{};
  FBO_Container_t Ping{};
  FBO_Container_t Pong{};
  bool TargetsValid = false;
  int TargetWidth = 0;
  int TargetHeight = 0;

  video::AsyncDecoder Media;
  video::VideoTexture Frame;
  bool VideoAttempted = false;
  bool VideoFailed = false;
  std::string VideoError;
  // Row-flip scratch for the decoded frame: the capture path samples its
  // source like the engine's own FBOs (row 0 at the bottom of the image),
  // while the decoder produces top-down rows for the Rml path.
  std::vector<unsigned char> FlippedPixels;

  unsigned int Generation = 0;
  std::string Uri;
  bool Published = false;

  void freeTargets() {
    auto *renderer = abc::MetaRenderer();
    if (renderer) {
      renderer->PushFrameBuffer();
      for (FBO_Container_t *target : {&Down, &Ping, &Pong}) {
        if (target->s_hBackBufferFBO)
          renderer->FreeFBO(target);
        *target = {};
      }
      renderer->PopFrameBuffer();
    }
    TargetsValid = false;
    TargetWidth = TargetHeight = 0;
  }

  void unpublish() {
    if (auto *rendererInterface = rmlui::renderInterface())
      rendererInterface->unregisterExternalTexture(kTexturePrefix);
    Published = false;
    Generation = 0;
    Uri.clear();
  }
};

Backdrop::Backdrop() : Data(std::make_unique<State>()) {}
Backdrop::~Backdrop() {
  shutdown();
}

void Backdrop::setMode(Mode mode) {
  Data->Requested = mode;
}
Mode Backdrop::requestedMode() const {
  return Data->Requested;
}
Mode Backdrop::effectiveMode() const {
  return Data->Effective;
}

void Backdrop::setVideoSource(const std::string &source) {
  if (Data->VideoSource == source)
    return;
  Data->VideoSource = source;
  Data->Media.close();
  Data->Frame.release();
  Data->VideoAttempted = false;
  Data->VideoFailed = false;
  Data->VideoError.clear();
}

const std::string &Backdrop::videoSource() const {
  return Data->VideoSource;
}

void Backdrop::setBlurRounds(int rounds) {
  Data->Rounds = std::clamp(rounds, 0, kMaxRounds);
}
int Backdrop::blurRounds() const {
  return Data->Rounds;
}

void Backdrop::invalidateTargets() {
  Data->unpublish();
  Data->freeTargets();
}

void Backdrop::shutdown() {
  if (!Data)
    return;
  Data->unpublish();
  Data->freeTargets();
  Data->Media.close();
  Data->Frame.release();
  Data->Uri.clear();
}

const std::string &Backdrop::uri() const {
  return Data->Uri;
}

Backdrop::Diagnostics Backdrop::diagnostics() const {
  Diagnostics out;
  out.Requested = static_cast<int>(Data->Requested);
  out.Effective = static_cast<int>(Data->Effective);
  out.Rounds = Data->Rounds;
  out.Width = Data->TargetWidth;
  out.Height = Data->TargetHeight;
  out.VideoOpen = Data->Media.isOpen();
  out.VideoSeconds = Data->Media.lastSeconds();
  return out;
}

void Backdrop::pumpVideo(double frameTime, bool Enabled) {
  State &state = *Data;
  if (!Enabled || state.Requested == Mode::kLive ||
      state.Requested == Mode::kSolid) {
    state.Media.pause();
    return;
  }
  if (state.VideoSource.empty() || state.VideoFailed)
    return;
  if (!state.Media.isOpen()) {
    if (state.VideoAttempted)
      return;
    state.VideoAttempted = true;
    // A background movie never needs more than 720 px of width: it is
    // blurred by two gaussian rounds before it reaches the screen.
    const std::string path = abc::ModPath(state.VideoSource.c_str());
    if (!state.Media.open(path.c_str(), 640, 360)) {
      state.VideoFailed = true;
      state.VideoError = state.Media.error();
      abc::ConsoleWarn("%s %s: %s\n", kSeenWarnTag, state.VideoSource.c_str(),
                       state.VideoError.c_str());
      return;
    }
    state.Media.setLooping(true);
  }
  if (!state.Media.advance(frameTime)) {
    if (!state.Media.error().empty()) {
      state.VideoFailed = true;
      abc::ConsoleWarn("%s %s: %s\n", kSeenWarnTag, state.VideoSource.c_str(),
                       state.Media.error().c_str());
    }
    return;
  }
  const video::FrameView &frame = state.Media.frame();
  if (frame.Pixels) {
    // The capture DrawTexturedQuad samples v=0 at the bottom edge of the
    // destination (the engine's FBO texture convention the live path
    // relies on), while the decoder hands us top-down rows for Rml's
    // (0,0)=top-left convention. Flip the rows for this consumer only,
    // so the blurred movie reaches the screen the right way up.
    const std::size_t stride = static_cast<std::size_t>(frame.Width) * 4;
    state.FlippedPixels.resize(stride * frame.Height);
    for (int row = 0; row < frame.Height; row++) {
      std::memcpy(state.FlippedPixels.data() +
                      static_cast<std::size_t>(row) * stride,
                  frame.Pixels +
                      static_cast<std::size_t>(frame.Height - 1 - row) * stride,
                  stride);
    }
    state.Frame.upload(state.FlippedPixels.data(), frame.Width, frame.Height);
  }
}

bool Backdrop::render() {
  State &state = *Data;
  auto *renderer = abc::MetaRenderer();
  auto *rendererInterface = rmlui::renderInterface();
  if (!renderer || !rendererInterface)
    return false;

  bool Republished = false;
  const auto Publish = [&state]() {
    state.Generation = static_cast<unsigned int>(++NextGeneration);
    state.Uri = std::string(kTexturePrefix) + std::to_string(state.Generation);
    state.Published = true;
  };

  const bool inLevel = services::nativeUiBridge().hasLevel();
  Mode mode = state.Requested;
  if (mode == Mode::kAuto)
    mode = inLevel ? Mode::kLive : Mode::kVideo;

  // Source resolution: a requested video without a frame yet degrades to the
  // live frame (and the other way round), so the backdrop is never missing.
  unsigned int sourceTexture = 0;
  if (mode == Mode::kLive && inLevel) {
    if (FBO_Container_t * final = renderer->GetFinalBufferFBO())
      sourceTexture = final->s_hBackBufferTex;
  }
  if (sourceTexture == 0 && state.Frame.valid()) {
    sourceTexture = static_cast<unsigned int>(state.Frame.textureId());
    if (sourceTexture != 0)
      mode = Mode::kVideo;
  }
  if (sourceTexture == 0 && mode != Mode::kLive) {
    if (FBO_Container_t * final = renderer->GetFinalBufferFBO())
      sourceTexture = final->s_hBackBufferTex;
    if (sourceTexture != 0)
      mode = Mode::kLive;
  }
  if (sourceTexture == 0) {
    // Nothing to blur: a flat backdrop still covers the window (the page's
    // own background colour shows through).
    if (state.Effective != Mode::kSolid) {
      state.unpublish();
      state.Uri.clear();
    }
    state.Effective = Mode::kSolid;
    return false;
  }
  state.Effective = mode;

  const int width = std::max(
      static_cast<int>(renderer->GetSwapChainWidth()) / kDownscale, kMinEdge);
  const int height = std::max(
      static_cast<int>(renderer->GetSwapChainHeight()) / kDownscale, kMinEdge);
  if (!state.TargetsValid || state.TargetWidth != width ||
      state.TargetHeight != height) {
    state.unpublish();
    state.freeTargets();
    renderer->PushFrameBuffer();
    const struct {
      FBO_Container_t *Target;
      const char *Name;
    } targets[] = {{&state.Down, "AbcUi backdrop source"},
                   {&state.Ping, "AbcUi backdrop blur A"},
                   {&state.Pong, "AbcUi backdrop blur B"}};
    for (const auto &target : targets) {
      target.Target->iWidth = width;
      target.Target->iHeight = height;
      renderer->GenFrameBuffer(target.Target, target.Name);
      renderer->FrameBufferColorTexture(target.Target, GL_RGBA8);
    }
    renderer->PopFrameBuffer();
    state.TargetsValid = true;
    state.TargetWidth = width;
    state.TargetHeight = height;
    // A fresh texture id behind the same registration key: publish a new
    // URI so Rml re-resolves once.
    Publish();
    Republished = true;
  }

  // The source can come back after a solid frame (unpublish() cleared the
  // URI while the FBOs stayed valid): publish again before capturing.
  if (!state.Published) {
    Publish();
    Republished = true;
  }

  // Capture: an opaque white fill first, then the source alpha-blended over
  // it. The fill forces the blur chain's alpha to 1 whatever the source's
  // alpha channel holds (the final buffer's alpha is not specified).
  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  renderer->PushFrameBuffer();
  renderer->BindFrameBuffer(&state.Down);
  renderer->Set2DEx(0, 0, width, height);
  renderer->ClearColor(const_cast<float *>(white));
  renderer->DrawFilledQuad(0, 0, width, height, white,
                           DRAW_FILLED_RECT_ALPHA_BLEND_ENABLED,
                           "abcui backdrop fill");
  renderer->DrawTexturedQuad(
      static_cast<int>(sourceTexture), 0, 0, width, height, white,
      DRAW_TEXTURED_RECT_ALPHA_BLEND_ENABLED, "abcui backdrop capture");
  renderer->Finish2D();
  renderer->PopFrameBuffer();

  // Gaussian rounds: horizontal then vertical, ping-ponging between the two
  // blur targets (the Renderer's own bloom chain shape). Zero rounds keep
  // the raw capture: `from` stays the Down target, which is what the
  // published texture then points at.
  FBO_Container_t *from = &state.Down;
  for (int round = 0; round < state.Rounds; round++) {
    renderer->BlurPass(from, &state.Ping, 1.0f, false);
    renderer->BlurPass(&state.Ping, &state.Pong, 1.0f, true);
    from = &state.Pong;
  }

  rendererInterface->registerExternalTexture(
      kTexturePrefix, static_cast<Rml::TextureHandle>(from->s_hBackBufferTex),
      width, height, nullptr);
  // The URI only changes with the texture (see above): the per-frame pixel
  // changes ride the same texture handle, so Rml's texture cache is not
  // disturbed and the element writes no DOM while the backdrop is steady.
  return Republished;
}

Backdrop &menuBackdrop() {
  static Backdrop Instance;
  return Instance;
}

} // namespace menubackdrop
