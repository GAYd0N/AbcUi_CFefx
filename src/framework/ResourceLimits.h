#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <algorithm>
namespace framework::ResourceLimits {
inline constexpr std::size_t MaxFileBytes = 64 * 1024 * 1024;
inline constexpr int MaxDimension = 4096;
inline bool imageBytes(int width, int height, std::size_t &bytes) {
  if (width <= 0 || height <= 0 || width > MaxDimension ||
      height > MaxDimension)
    return false;
  bytes =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
  return bytes <= MaxFileBytes;
}
inline bool span(std::size_t offset, std::size_t count, std::size_t size) {
  return offset <= size && count <= size - offset;
}
inline bool crop(int left, int top, int right, int bottom, int atlasWidth,
                 int atlasHeight, int &x, int &y, int &width, int &height) {
  if (right <= left || bottom <= top || atlasWidth <= 0 || atlasHeight <= 0)
    return false;
  x = (std::max)(0, left);
  y = (std::max)(0, top);
  const int endX = (std::min)(right, atlasWidth),
            endY = (std::min)(bottom, atlasHeight);
  if (endX <= x || endY <= y)
    return false;
  width = endX - x;
  height = endY - y;
  std::size_t bytes;
  return imageBytes(width, height, bytes);
}
inline bool resourceName(std::string_view name) {
  if (name.empty() || name.size() > 127 || name.front() == '/' ||
      name.find("..") != name.npos)
    return false;
  for (unsigned char c : name)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '/' ||
          c == '.'))
      return false;
  return true;
}
} // namespace framework::ResourceLimits
