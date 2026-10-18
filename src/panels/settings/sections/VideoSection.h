#pragma once
// Video controls projection/cache; no page navigation, engine commands or input
// ownership.
#include <vector>
#include <utility>
namespace Rml {
class ElementDocument;
}
namespace services::settings {
class VideoModeModel;
}
namespace panels::settings::sections {
class VideoSection {
public:
  void render(Rml::ElementDocument *Document,
              services::settings::VideoModeModel *Model);
  void reset() {
    OptionsShown.clear();
  }

private:
  std::vector<std::pair<int, int>> OptionsShown;
};
} // namespace panels::settings::sections
