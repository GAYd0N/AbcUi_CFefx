// framework::glyphs implementation - see GlyphText.h.
#include "GlyphText.h"

namespace framework {

namespace glyphs {

namespace {

// Glyph file stem for a character, nullptr when the set has none.
const char *glyphFile(char c) {
  switch (c) {
  case '0':
    return "0";
  case '1':
    return "1";
  case '2':
    return "2";
  case '3':
    return "3";
  case '4':
    return "4";
  case '5':
    return "5";
  case '6':
    return "6";
  case '7':
    return "7";
  case '8':
    return "8";
  case '9':
    return "9";
  case '/':
    return "slash";
  case 'x':
  case 'X':
    return "x";
  case '%':
    return "percent";
  default:
    return nullptr;
  }
}

} // namespace

bool supports(const std::string &text) {
  for (const char c : text) {
    if (!glyphFile(c))
      return false;
  }
  return true;
}

namespace {

// One glyph cell; `outline` adds the black silhouette layer under the ink.
std::string glyphMarkup(char c, const char *tint, bool outline) {
  const char *file = glyphFile(c);
  if (!file)
    return {};
  std::string out = "<span class=\"hud-glyph\">";
  if (outline) {
    out += "<img class=\"glyph-outline\" src=\"tex://";
    out += kOutlineRoot;
    out += file;
    out += ".png\"/>";
  }
  out += "<img class=\"glyph-ink\" src=\"texadd://";
  out += kGlyphRoot;
  out += file;
  out += ".png\" style=\"image-color: ";
  out += tint ? tint : "#ffb000";
  out += "\"/></span>";
  return out;
}

std::string markupWith(const std::string &text, const char *tint,
                       bool outline) {
  std::string out;
  out.reserve(text.size() * 240);
  for (const char c : text) {
    const std::string one = glyphMarkup(c, tint, outline);
    if (one.empty())
      return {};
    out += one;
  }
  return out;
}

} // namespace

std::string glyph(char c, const char *tint) {
  return glyphMarkup(c, tint, true);
}

std::string markup(const std::string &text, const char *tint) {
  return markupWith(text, tint, true);
}

std::string inkMarkup(const std::string &text, const char *tint) {
  return markupWith(text, tint, false);
}

} // namespace glyphs

} // namespace framework
