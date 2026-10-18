#pragma once

// New implementation. Engine-independent <model> attribute validation.
// Target consumes the validated options; ElementModel supplies string values.
#include <array>
#include <functional>
#include <string>
#include <string_view>

namespace modelpreview {

// The shipped generic player model: every resolution chain ends here (it is
// the stock `models/player.mdl` the card's document also defaults to).
constexpr const char *kGenericModelPath = "models/player.mdl";

// Resolves a Studio path spelling the way the shipped cvars spell one: a
// value that already names a file (".../*.mdl") is used verbatim, anything
// else is read as the model's directory name under models/player/. An empty
// or unusable spelling falls back to `Fallback` (same grammar) and then to
// the generic player model, so the card is never left without a source.
std::string resolveModelPath(std::string_view Spelling,
                             std::string_view Fallback);

// Existence probe for one resolved Studio path (engine filesystem fact).
using ModelExists = std::function<bool(const std::string &)>;

// The character card's source: `Spelling` (the menu's own override cvar; empty
// when unset) resolves through the grammar above with `PlayerModel` as the
// spelling fallback. A path the engine cannot see is not presented: it falls
// back to the shipped generic model, so the card never points at a source the
// preview renderer cannot load (an unloadable source used to leave the box
// blank/white). An empty probe means "no filesystem fact" - the resolved
// spelling is then trusted.
std::string resolveCardModelPath(std::string_view Spelling,
                                 std::string_view PlayerModel,
                                 const ModelExists &Exists);

// The card's camera, light and background in the Renderer's entity_view_t
// units: the Renderer only provides the isolated view scope, so the preview
// owns what the view shows and how it is lit.
struct PreviewView {
  std::array<float, 3> Origin{120, 0, 0};
  std::array<float, 3> Angles{0, 180, 0};
  float VerticalFov = 40;
  float NearPlane = 1;
  float FarPlane = 4096;
  int Ambient = 96;
  int Shade = 96;
  std::array<float, 3> LightColor{1, 1, 1};
  std::array<float, 3> LightDirection{-1, 0, -1};
  std::array<float, 4> Background{0, 0, 0, 0};
  bool Lighting = true;
};

struct Options {
  PreviewView View;
  std::string Source;
  std::array<float, 3> Origin{};
  std::array<float, 3> Angles{};
  std::array<int, 3> RenderColor{255, 255, 255};
  std::array<int, 4> Controllers{};
  std::array<int, 2> Blending{};
  int Mouth = 0;
  int ColorMap = 0;
  int RenderMode = 0;
  int RenderFx = 0;
  int RenderAmount = 255;
  int Sequence = 0;
  int Skin = 0;
  int Body = 0;
  float Frame = 0;
  float FrameRate = 1;
  float Scale = 1;
  bool Animate = true;
  // Auto framing: derive the camera distance from the model's own bounding
  // box so any player model (a 72-unit human, a 35-unit creature, a large
  // monster) fills the same box. The camera-origin/fov attributes are then
  // ignored for the placement; FrameMargin scales the fitted extent
  // (1.0 = the box is exactly filled, 1.15 leaves a margin).
  bool AutoFrame = false;
  float FrameMargin = 1.15f;
  // Pointer drag rotation (the multiplayer settings' inspection preview):
  // while the primary button is held, horizontal motion turns the entity and
  // vertical motion tilts it within a clamped range.
  bool DragRotate = false;

  // Unknown DOM attributes (id/class/style) are not errors. Invalid known
  // attributes leave the previous value unchanged and return false.
  bool set(std::string_view Name, std::string_view Value);
  static bool isModelPath(std::string_view Path);
};

} // namespace modelpreview
