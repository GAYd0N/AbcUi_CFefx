#pragma once

#include "Options.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>

namespace modelpreview {

struct FramedCamera {
  std::array<float, 3> Origin{};
  std::array<float, 3> Angles{};
};

using FramePoint = std::array<float, 3>;

namespace framingdetail {
struct Basis {
  FramePoint Forward, Right, Up;
};

inline Basis basis(const FramePoint &Angles) {
  constexpr float Radians = 3.14159265358979f / 180.0f;
  const float sp = std::sin(Angles[0] * Radians);
  const float cp = std::cos(Angles[0] * Radians);
  const float sy = std::sin(Angles[1] * Radians);
  const float cy = std::cos(Angles[1] * Radians);
  const float sr = std::sin(Angles[2] * Radians);
  const float cr = std::cos(Angles[2] * Radians);
  return {{cp * cy, cp * sy, -sp},
          {-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp},
          {cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp}};
}

inline float dot(const FramePoint &A, const FramePoint &B) {
  return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}
} // namespace framingdetail

// Tight perspective fit of the visible, already-posed world-space vertices.
// Intersect the horizontal/vertical camera-position intervals for every point;
// their closest feasible intersection fills at least one viewport axis. Unlike
// a world AABB, this does not fit empty space around a diagonal held weapon.
inline std::optional<FramedCamera>
frameCameraPoints(const Options &Config, std::span<const FramePoint> Points,
                  float HorizontalFov, float Aspect) {
  if (Points.empty())
    return std::nullopt;
  const auto Camera = framingdetail::basis(Config.View.Angles);
  constexpr float HalfRadians = 3.14159265358979f / 360.0f;
  const float TanHorizontal =
      std::tan(std::clamp(HorizontalFov, 30.0f, 170.0f) * HalfRadians) /
      Config.FrameMargin;
  const float TanVertical = TanHorizontal / std::max(Aspect, 0.01f);
  const float Infinity = std::numeric_limits<float>::infinity();
  float LowerRight = -Infinity, UpperRight = Infinity;
  float LowerUp = -Infinity, UpperUp = Infinity;
  float MinDepth = Infinity, MaxDepth = -Infinity;
  for (const auto &Point : Points) {
    for (float Value : Point) {
      if (!std::isfinite(Value))
        return std::nullopt;
    }
    const float Depth = framingdetail::dot(Point, Camera.Forward);
    const float Right = framingdetail::dot(Point, Camera.Right);
    const float Up = framingdetail::dot(Point, Camera.Up);
    LowerRight = std::max(LowerRight, Right - Depth * TanHorizontal);
    UpperRight = std::min(UpperRight, Right + Depth * TanHorizontal);
    LowerUp = std::max(LowerUp, Up - Depth * TanVertical);
    UpperUp = std::min(UpperUp, Up + Depth * TanVertical);
    MinDepth = std::min(MinDepth, Depth);
    MaxDepth = std::max(MaxDepth, Depth);
  }
  const float Forward = std::min(
      {(UpperRight - LowerRight) / (2 * TanHorizontal),
       (UpperUp - LowerUp) / (2 * TanVertical),
       MinDepth - Config.View.NearPlane, (MinDepth + MaxDepth) * 0.5f - 8.0f});
  const float Right = (LowerRight + UpperRight) * 0.5f;
  const float Up = (LowerUp + UpperUp) * 0.5f;
  FramedCamera Result;
  Result.Angles = Config.View.Angles;
  for (int i = 0; i < 3; ++i)
    Result.Origin[i] = Camera.Forward[i] * Forward + Camera.Right[i] * Right +
                       Camera.Up[i] * Up;
  return Result;
}

// Bootstrap/fallback fit before the first Studio pose is available. Min/Max
// are scaled model-space bounds; the normal fit uses actual posed vertices.
inline FramedCamera frameCamera(const Options &Config, const FramePoint &Min,
                                const FramePoint &Max, float HorizontalFov,
                                float Aspect) {
  const auto Model = framingdetail::basis(Config.Angles);
  std::array<FramePoint, 8> Corners{};
  for (int Corner = 0; Corner < 8; ++Corner) {
    const float x = (Corner & 1) ? Max[0] : Min[0];
    const float y = (Corner & 2) ? Max[1] : Min[1];
    const float z = (Corner & 4) ? Max[2] : Min[2];
    for (int i = 0; i < 3; ++i)
      Corners[Corner][i] = Config.Origin[i] + Model.Forward[i] * x -
                           Model.Right[i] * y + Model.Up[i] * z;
  }
  return frameCameraPoints(Config, Corners, HorizontalFov, Aspect)
      .value_or(FramedCamera{Config.View.Origin, Config.View.Angles});
}

} // namespace modelpreview
