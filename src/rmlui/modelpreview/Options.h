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

// Action groups (the menu stance models) live loose under the plugin's own
// game-content folder: module ZIPs are UI resources only, an engine Studio
// load needs a real file in the game search path.
constexpr const char *kActionModelDir = "abc/abcui/models/menu_action/";
constexpr const char *kActionModelDefault = "display_male";

// Resolves a Studio path spelling the way the shipped cvars spell one: a
// value that already names a file (".../*.mdl") is used verbatim, anything
// else is read as the model's directory name under models/player/. An empty
// or unusable spelling falls back to `Fallback` (same grammar) and then to
// the generic player model, so the card is never left without a source.
std::string resolveModelPath(std::string_view Spelling,
                             std::string_view Fallback);

// Existence probe for one resolved Studio path (engine filesystem fact).
using ModelExists = std::function<bool(const std::string &)>;

// The main page's action group (cvar `abc_ui_menu_action_model`): a bare
// spelling names a group in kActionModelDir ("display_male" ->
// "abc/abcui/models/menu_action/display_male.mdl"), a .mdl path
// file is used verbatim. Empty or invalid spelling selects the default group.
// A missing group yields an empty path: the card then draws its own model
// without the follow/merge chain.
std::string resolveActionModelPath(std::string_view Spelling,
                                   const ModelExists &Exists);

// The main page's weapon model (cvar `abc_ui_menu_weapon_model`): only a
// complete game-relative .mdl path with a directory is accepted, verbatim
// ("models/p_m16.mdl"). No filename aliases or inferred paths. Empty (or
// unusable) means no weapon. <model weapon> uses the same validation.
std::string resolveWeaponModelPath(std::string_view Spelling);

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

// The card's camera, Studio lighting and background. LightDirection is the
// world-space light-ray direction, normalized by the draw bridge.
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
  // The displayed player model ("src").
  std::string Source;
  // The action group that drives the displayed model and the weapon
  // ("action-src"): evaluated without rendering before each follow draw so
  // matching bones consume its saved pose. Empty keeps single-entity behaviour.
  std::string ActionSource;
  // The weapon model merged onto the action group ("weapon"). Empty = none.
  std::string WeaponSource;
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
  // Auto framing: fit the visible posed player/weapon mesh vertices so
  // models of different sizes fill the same box. Before the first sampled
  // pose, the displayed player's bounds bootstrap the view. Camera-origin/fov
  // attributes are then
  // ignored for placement, while camera-angles sets the viewing direction.
  // FrameMargin scales the fitted extent
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
  // A safe, relative Studio path: ends with ".mdl" (case-insensitive), no
  // "..", no backslash/colon/control bytes and no leading slash. Both
  // "models/..." content paths and the plugin's "abc/..." action groups pass.
  static bool isModelPath(std::string_view Path);
};

} // namespace modelpreview
