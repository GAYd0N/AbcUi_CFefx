// New implementation; see Options.h. Parsing never forwards NaN, infinity,
// trailing junk or unsafe entity modes to the native Studio renderer.
#include "Options.h"

#include <abc/Parse.h>

#include <algorithm>
#include <charconv>
#include <cmath>

namespace modelpreview {
namespace {
template <class T> bool number(std::string_view Text, T &Output, T Min, T Max) {
  const auto Value = abc::ParseNumber<T>(Text);
  if (!Value || !std::isfinite(static_cast<double>(*Value)) || *Value < Min ||
      *Value > Max)
    return false;
  Output = *Value;
  return true;
}

bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

template <class T, size_t N>
bool vector(std::string_view Text, std::array<T, N> &Output, T Min, T Max) {
  std::array<T, N> Values{};
  std::size_t at = 0;
  for (T &Value : Values) {
    while (at < Text.size() && isSpace(Text[at]))
      ++at;
    const std::size_t begin = at;
    while (at < Text.size() && !isSpace(Text[at]))
      ++at;
    if (begin == at || !number(Text.substr(begin, at - begin), Value, Min, Max))
      return false;
  }
  while (at < Text.size() && isSpace(Text[at]))
    ++at;
  if (at != Text.size())
    return false;
  Output = Values;
  return true;
}
} // namespace

bool Options::isModelPath(std::string_view Path) {
  return Path.starts_with("models/") && Path.ends_with(".mdl") &&
         Path.size() < 64 && Path.find("..") == Path.npos &&
         Path.find_first_of("\\:\r\n\t") == Path.npos;
}

namespace {
// One spelling through the models/player/ directory grammar.
std::string spell(std::string_view Name) {
  if (Name.empty())
    return {};
  if (Name.ends_with(".mdl"))
    return std::string(Name);
  return "models/player/" + std::string(Name) + "/" + std::string(Name) +
         ".mdl";
}
} // namespace

std::string resolveModelPath(std::string_view Spelling,
                             std::string_view Fallback) {
  std::string Resolved = spell(Spelling);
  if (!Options::isModelPath(Resolved))
    Resolved = spell(Fallback);
  if (!Options::isModelPath(Resolved))
    Resolved = kGenericModelPath;
  return Resolved;
}

std::string resolveCardModelPath(std::string_view Spelling,
                                 std::string_view PlayerModel,
                                 const ModelExists &Exists) {
  std::string Card = resolveModelPath(Spelling, PlayerModel);
  if (Exists && !Exists(Card))
    Card = kGenericModelPath;
  return Card;
}

bool Options::set(std::string_view Name, std::string_view Value) {
  if (Name == "src") {
    if (!Value.empty() && !isModelPath(Value))
      return false;
    Source = Value;
    return true;
  }
  if (Name == "camera-origin")
    return vector<float, 3>(Value, View.Origin, -16384, 16384);
  if (Name == "camera-angles")
    return vector<float, 3>(Value, View.Angles, -360, 360);
  if (Name == "fov")
    return number(Value, View.VerticalFov, 2.0f, 175.0f);
  if (Name == "near")
    return number(Value, View.NearPlane, 0.01f, 64.0f);
  if (Name == "far")
    return number(Value, View.FarPlane, 65.0f, 32768.0f);
  if (Name == "ambient")
    return number(Value, View.Ambient, 0, 128);
  if (Name == "shade")
    return number(Value, View.Shade, 0, 255);
  if (Name == "light-color")
    return vector<float, 3>(Value, View.LightColor, 0, 1);
  if (Name == "light-direction") {
    std::array<float, 3> Direction{};
    if (!vector<float, 3>(Value, Direction, -1, 1) ||
        (Direction[0] == 0 && Direction[1] == 0 && Direction[2] == 0))
      return false;
    View.LightDirection = Direction;
    return true;
  }
  if (Name == "background")
    return vector<float, 4>(Value, View.Background, 0, 1);
  if (Name == "origin")
    return vector<float, 3>(Value, Origin, -16384, 16384);
  if (Name == "angles")
    return vector<float, 3>(Value, Angles, -360, 360);
  if (Name == "rendercolor")
    return vector<int, 3>(Value, RenderColor, 0, 255);
  if (Name == "controllers")
    return vector<int, 4>(Value, Controllers, 0, 255);
  if (Name == "blending")
    return vector<int, 2>(Value, Blending, 0, 255);
  if (Name == "mouth")
    return number(Value, Mouth, 0, 255);
  if (Name == "colormap")
    return number(Value, ColorMap, 0, 65535);
  if (Name == "rendermode") {
    int Mode = 0;
    // kRenderGlow is a sprite-only mode.
    if (!number(Value, Mode, 0, 5) || Mode == 3)
      return false;
    RenderMode = Mode;
    return true;
  }
  if (Name == "renderfx") {
    int Fx = 0;
    // Dead-player rendering dereferences a network player state.
    if (!number(Value, Fx, 0, 20) || Fx == 17)
      return false;
    RenderFx = Fx;
    return true;
  }
  if (Name == "renderamt")
    return number(Value, RenderAmount, 0, 255);
  if (Name == "sequence")
    return number(Value, Sequence, 0, 65535);
  if (Name == "skin")
    return number(Value, Skin, 0, 65535);
  if (Name == "body")
    return number(Value, Body, 0, 65535);
  if (Name == "frame")
    return number(Value, Frame, 0.0f, 255.0f);
  if (Name == "framerate")
    return number(Value, FrameRate, -16.0f, 16.0f);
  if (Name == "scale")
    return number(Value, Scale, 0.01f, 16.0f);
  if (Name == "animate" || Name == "lighting") {
    if (Value != "true" && Value != "false" && Value != "1" && Value != "0")
      return false;
    const bool Enabled = Value == "true" || Value == "1";
    if (Name == "animate")
      Animate = Enabled;
    else
      View.Lighting = Enabled;
  }
  if (Name == "auto-frame") {
    if (Value != "true" && Value != "false" && Value != "1" && Value != "0")
      return false;
    AutoFrame = Value == "true" || Value == "1";
    return true;
  }
  if (Name == "frame-margin")
    return number(Value, FrameMargin, 0.5f, 4.0f);
  if (Name == "drag-rotate") {
    if (Value != "true" && Value != "false" && Value != "1" && Value != "0")
      return false;
    DragRotate = Value == "true" || Value == "1";
    return true;
  }
  return true;
}
} // namespace modelpreview
