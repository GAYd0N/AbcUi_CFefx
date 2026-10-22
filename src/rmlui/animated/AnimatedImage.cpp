// rmlui::animated::AnimatedImage implementation - see AnimatedImage.h.
#include "AnimatedImage.h"

#include "WebPDecoder.h"

#include "../../framework/ResourceFile.h"
#include "../../framework/ResourceLimits.h"

namespace rmlui::animated {

namespace {

using detail::Backend;

// Registered decoders, probed in order. Every entry reports its own probe so
// the caller never guesses from the file extension (mod trees, downloads and
// Steam avatar buffers all carry whatever name they like).
const std::vector<const Backend *> &backends() {
  static const std::vector<const Backend *> List = {&detail::webpBackend()};
  return List;
}

const Backend *probeFormat(const std::vector<unsigned char> &bytes) {
  if (bytes.empty())
    return nullptr;
  for (const Backend *backend : backends())
    if (backend->Probe(bytes.data(), bytes.size()))
      return backend;
  return nullptr;
}

std::unique_ptr<AnimatedImage> openBytes(std::vector<unsigned char> bytes,
                                         std::string source,
                                         std::string &error) {
  error.clear();
  const Backend *backend = probeFormat(bytes);
  if (!backend) {
    error = "unsupported animated image format";
    return nullptr;
  }
  std::unique_ptr<AnimatedImage> image =
      backend->Open(std::move(bytes), std::move(source), error);
  if (!image && error.empty())
    error = "animation decode failed";
  return image;
}

} // namespace

std::unique_ptr<AnimatedImage> openFile(const std::string &path,
                                        std::string &error) {
  error.clear();
  std::vector<unsigned char> bytes;
  if (!framework::ResourceFile::read(path, bytes,
                                     framework::ResourceLimits::MaxFileBytes)) {
    error = "cannot read " + path;
    return nullptr;
  }
  return openBytes(std::move(bytes), path, error);
}

std::unique_ptr<AnimatedImage> openMemory(std::vector<unsigned char> bytes,
                                          std::string source,
                                          std::string &error) {
  return openBytes(std::move(bytes), std::move(source), error);
}

} // namespace rmlui::animated
