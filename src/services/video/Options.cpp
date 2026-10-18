// services::video::Options implementation - see Options.h.
#include "Options.h"

#include <algorithm>
#include <cctype>
#include <abc/Parse.h>

#include <cmath>

namespace video {

namespace {

bool boolean(std::string_view Text, bool &Output) {
  if (Text == "true" || Text == "1") {
    Output = true;
    return true;
  }
  if (Text == "false" || Text == "0") {
    Output = false;
    return true;
  }
  return false;
}

bool positiveFloat(std::string_view Text, float Min, float Max, float &Output) {
  const auto Value = abc::ParseFloat(Text);
  if (!Value || !std::isfinite(*Value) || *Value < Min || *Value > Max)
    return false;
  Output = *Value;
  return true;
}

} // namespace

bool Options::isVideoPath(std::string_view Path) {
  if (Path.empty() || Path.size() >= 192)
    return false;
  if (Path.find("..") != Path.npos ||
      Path.find_first_of("\\:\r\n\t") != Path.npos)
    return false;
  static constexpr std::string_view kExtensions[] = {
      ".mp4", ".webm", ".mkv", ".avi", ".mov", ".m4v", ".ivf"};
  for (const auto extension : kExtensions) {
    if (Path.size() > extension.size()) {
      const auto Tail = Path.substr(Path.size() - extension.size());
      if (std::equal(Tail.begin(), Tail.end(), extension.begin(),
                     [](char left, char right) {
                       return std::tolower(static_cast<unsigned char>(left)) ==
                              std::tolower(static_cast<unsigned char>(right));
                     }))
        return true;
    }
  }
  return false;
}

bool Options::set(std::string_view Name, std::string_view Value) {
  if (Name == "src") {
    if (!Value.empty() && !isVideoPath(Value))
      return false;
    Source = std::string(Value);
    return true;
  }
  if (Name == "loop")
    return boolean(Value, Loop);
  if (Name == "autoplay")
    return boolean(Value, Autoplay);
  if (Name == "muted") {
    // Accepted and inert by construction: the decoder never opens an audio
    // stream, so every video element is silent (the background video
    // requirement). Kept as a known attribute so documents can state it.
    bool ignored = true;
    return boolean(Value, ignored);
  }
  if (Name == "speed")
    return positiveFloat(Value, 0.05f, 8.0f, Speed);
  if (Name == "fit") {
    if (Value == "cover") {
      FitMode = Fit::kCover;
      return true;
    }
    if (Value == "contain") {
      FitMode = Fit::kContain;
      return true;
    }
    if (Value == "fill") {
      FitMode = Fit::kFill;
      return true;
    }
    return false;
  }
  return true;
}

} // namespace video
