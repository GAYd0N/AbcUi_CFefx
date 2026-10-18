// gameui::SprayMaker implementation - see SprayMaker.h.
#include "SprayMaker.h"

#include "SettingsResources.h"
#include "framework/ImageLoader.h"
#include "framework/Localization.h"
#include "framework/ResourceLimits.h"
#include "rmlui/texture/DynamicTexture.h"
#include "services/spray/Encoder.h"

#include <abc/Format.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace gameui {
namespace {

std::string tokenText(const char *token) {
  // The lang table keys carry no leading '#'.
  std::string key = token && *token == '#' ? token + 1 : (token ? token : "");
  std::string text;
  if (framework::Localization::translate(text, key))
    return text;
  return token ? token : std::string();
}

} // namespace

SprayMaker::SprayMaker(Result result) : ResultSink(std::move(result)) {}

SprayMaker::~SprayMaker() {
  shutdown();
}

void SprayMaker::shutdown() {
  Texture.reset();
}

void SprayMaker::run(const std::string &path) {
  const auto fail = [this](const char *token, const std::string &detail) {
    std::string message = tokenText(token);
    if (!detail.empty())
      message += " (" + detail + ")";
    if (ResultSink)
      ResultSink(false, message, {});
  };
  // Read the picked file (bounded: the same cap the decoders use).
  std::ifstream stream(std::filesystem::path(path),
                       std::ios::binary | std::ios::ate);
  const auto size = stream ? stream.tellg() : std::streampos(-1);
  if (size <= 0 || size > static_cast<std::streamoff>(
                              framework::ResourceLimits::MaxFileBytes)) {
    fail("#Settings_SprayMakerReadError", {});
    return;
  }
  std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
  stream.seekg(0);
  if (!stream.read(reinterpret_cast<char *>(bytes.data()), size)) {
    fail("#Settings_SprayMakerReadError", {});
    return;
  }
  stream.close();
  // Decode (FreeImage, format detected from the content).
  framework::ImageLoader::DecodedImage image;
  if (!framework::ImageLoader::loadFromMemory(bytes.data(), bytes.size(),
                                              image)) {
    fail("#Settings_SprayMakerDecodeError", {});
    return;
  }
  // Encode the WAD3 spray and drop it where the engine reads its own
  // tempdecal.wad (the original wrote the same file).
  services::spray::EncodedSpray spray;
  std::string error;
  if (!services::spray::encode(image, spray, error)) {
    fail("#Settings_SprayMakerEncodeError", error);
    return;
  }
  if (!settings::writeModFile("tempdecal.wad", spray.Bytes, error)) {
    fail("#Settings_SprayMakerWriteError", error);
    return;
  }
  const std::string message =
      abc::Format("{0}: {1}x{2}", tokenText("#Settings_SprayMakerDone"),
                  std::to_string(spray.Width), std::to_string(spray.Height));
  std::string previewUri;
  if (!Texture)
    Texture =
        std::make_unique<services::texture::DynamicTexture>("spraytex://");
  if (Texture->upload(spray.Preview.Pixels.data(), spray.Preview.Width,
                      spray.Preview.Height))
    previewUri = Texture->uri();
  if (ResultSink)
    ResultSink(true, message, previewUri);
}

} // namespace gameui
