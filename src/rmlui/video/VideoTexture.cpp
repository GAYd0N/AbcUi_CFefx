#include "VideoTexture.h"

namespace video {
namespace {
// Process-unique registration prefix per texture instance, so two video
// surfaces can never share one external registration.
std::string makePrefix() {
  static unsigned long long Counter = 0;
  return "videotex://" + std::to_string(++Counter) + "/";
}
} // namespace

VideoTexture::VideoTexture() : Texture(makePrefix()) {}
VideoTexture::~VideoTexture() = default;

} // namespace video
