#pragma once

// Output snapshots keep the caller's capacity across repeated calls. The
// zero-size fallback is legacy-only: it requires a full current-size object.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace framework::abi {

template <typename T> void copySnapshot(void *out, const T &full) {
  static_assert(std::is_standard_layout_v<T> &&
                std::is_trivially_copyable_v<T>);
  static_assert(offsetof(T, structSize) == 0);
  static_assert(sizeof(full.structSize) == sizeof(uint32_t));
  if (!out)
    return;
  uint32_t capacity = 0;
  std::memcpy(&capacity, out, sizeof(capacity));
  if (capacity == 0) {
    std::memcpy(out, &full, sizeof(full));
  } else if (capacity >= sizeof(capacity)) {
    const size_t bytes = std::min<size_t>(capacity, sizeof(full));
    // Do not replace the capacity with the producer's larger structSize.
    std::memcpy(static_cast<unsigned char *>(out) + sizeof(capacity),
                reinterpret_cast<const unsigned char *>(&full) +
                    sizeof(capacity),
                bytes - sizeof(capacity));
  }
}

inline bool validArrayLayout(uint32_t count, uint32_t stride) {
  return stride >= sizeof(uint32_t) &&
         count <= std::numeric_limits<size_t>::max() / stride;
}

inline void *arrayElement(void *out, uint32_t index, uint32_t stride) {
  return static_cast<unsigned char *>(out) +
         static_cast<size_t>(index) * stride;
}

// Validate every returned row before writing any of them. Ex interfaces do
// not accept the legacy zero-size fallback or a capacity beyond the stride.
inline bool validSnapshotArray(void *out, uint32_t count, uint32_t stride) {
  if (!validArrayLayout(count, stride) || (!out && count != 0))
    return false;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t capacity = 0;
    std::memcpy(&capacity, arrayElement(out, i, stride), sizeof(capacity));
    if (capacity < sizeof(capacity) || capacity > stride)
      return false;
  }
  return true;
}

} // namespace framework::abi
