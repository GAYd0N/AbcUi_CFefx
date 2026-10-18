#pragma once

// framework::glyphs - additive HUD ink with a black outline.
//
// RmlUi always blends text and fills source-over, so a numeral cannot be
// drawn with the additive blend the HUD sprites use (kRenderTransAdd). These
// helpers compose each glyph as a two-layer <img> stack:
//   .glyph-outline  tex://.../glyph/outline/<n>.png  black silhouette, drawn
//                   source-over behind the ink - the outline the HUD keeps
//                   for readability on bright maps;
//   .glyph-ink      texadd://.../glyph/<n>.png       the amber core (eroded so
//                   the outline ring stays visible), added to the scene.
// Black adds nothing in the additive pass, which is why the ring has to be its
// own straight-alpha layer.
//
// The glyph set is a fixed cell (82x128 source: 8 top pad, 96 glyph, 24 below
// the baseline) so the ink lands on the text baseline at 0.80em and no glyph
// is clipped; base.rcss sizes the stack in em. Callers keep a text fallback:
// markup() returns an empty string when a character has no glyph.

#include <string>

namespace framework {

namespace glyphs {

// Asset roots (no scheme prefix; the URI scheme is added by glyph()).
constexpr const char *kGlyphRoot = "abc/abcui/hud/img/glyph/";
constexpr const char *kOutlineRoot = "abc/abcui/hud/img/glyph/outline/";

// Default tints for the HUD accent (base.rcss owns the palette, but markup
// composed in code needs the literal).
constexpr const char *kAccentTint = "rgb(255, 176, 0)";
constexpr const char *kAccentDimTint = "rgba(255, 176, 0, 125)";

// Additive meter strip (white, alpha kept) for bar fills.
constexpr const char *kMeterUri =
    "texadd://abc/abcui/hud/img/glyph/meter.png";

// True when every character of `text` has an additive glyph.
bool supports(const std::string &text);

// Markup for one glyph, tinted with the RCSS colour string `tint`; empty when
// the character has no glyph.
std::string glyph(char c, const char *tint);

// Markup for a whole string; empty when any character is unsupported.
std::string markup(const std::string &text, const char *tint);

// Ink-only variant: the additive glyph without the black outline layer, for
// readouts that sit on their own dark backing and do not want the ring.
std::string inkMarkup(const std::string &text, const char *tint);

} // namespace glyphs

} // namespace framework
