#pragma once

// framework::TextEncoding - UTF-8 boundary hygiene for engine text events
// (T07 step 3). The engine's text source (SDL_EVENT_TEXT_INPUT /
// SDL_EVENT_TEXT_EDITING, see rmlui/TextInput.h) delivers NUL-terminated
// UTF-8 inside a fixed 32-byte event buffer: a long composition string is
// byte-truncated by the platform and can end mid-sequence, and third-party
// IMEs are simply not trusted to emit only well-formed Unicode.
//
// Pure string code: no engine, Rml or Windows includes, so the exact
// production sanitizer links into offline tests.

#include <cstddef>
#include <string_view>

namespace framework::text {

// Returns the longest prefix of `data` that is well-formed UTF-8, with
// malformed bytes dropped (not replaced): decoding continues after each
// maximal illegal subsequence. Well-formed means RFC 3629 UTF-8 - no
// overlong encodings, no UTF-16 surrogate halves (CESU-8), nothing above
// U+10FFFF. A truncated final sequence (the 31-byte event-buffer cut) is
// dropped too; a later event resends it whole. Supplementary-plane
// characters survive as their normal 4-byte UTF-8 form.
std::string_view sanitizeUtf8(const char *data, std::size_t length);

// NUL-terminated convenience overload (stops at the first NUL).
std::string_view sanitizeUtf8(const char *data);

// UTF-8 byte offset -> character (code point) count, for translating the
// platform's byte-offset cursor inside a composition string to RmlUi's
// character-index caret. Clamps at the first malformed sequence.
int countCharacters(std::string_view utf8);

} // namespace framework::text
