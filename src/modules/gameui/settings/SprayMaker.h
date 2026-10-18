#pragma once

// gameui::SprayMaker - the module half of the settings spray maker.
//
// The settings page only picks a file. Decode, encode, write and the preview
// texture upload live here, away from GameUiModule; the answer goes back
// through a small result callback (outcome, message, preview URI).

#include <functional>
#include <memory>
#include <string>

namespace services::texture {
class DynamicTexture;
}

namespace gameui {

class SprayMaker {
public:
  using Result = std::function<void(bool ok, const std::string &message,
                                    const std::string &previewUri)>;

  explicit SprayMaker(Result result);
  ~SprayMaker();
  SprayMaker(const SprayMaker &) = delete;
  SprayMaker &operator=(const SprayMaker &) = delete;

  void run(const std::string &path);
  // Releases the preview texture while the renderer is still alive.
  void shutdown();

private:
  Result ResultSink;
  std::unique_ptr<services::texture::DynamicTexture> Texture;
};

} // namespace gameui
