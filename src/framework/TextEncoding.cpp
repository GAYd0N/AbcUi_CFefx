// framework::TextEncoding implementation - see TextEncoding.h.
#include "TextEncoding.h"

#include <cstring>

namespace framework::text {

namespace {

// Sequence length from a lead byte, or 0 for anything that cannot start a
// well-formed sequence (continuation bytes, 0xF8..0xFF). RFC 3629 caps
// sequences at 4 bytes; longer forms are overlong by definition.
unsigned sequenceLength(unsigned char lead) {
  if (lead < 0x80)
    return 1;
  if ((lead & 0xE0) == 0xC0)
    return 2;
  if ((lead & 0xF0) == 0xE0)
    return 3;
  if ((lead & 0xF8) == 0xF0)
    return 4;
  return 0;
}

// Minimum code point for each sequence length; shorter encodings of these
// values are overlong and rejected.
std::size_t minimumCodePoint(unsigned length) {
  constexpr std::size_t kMinimums[] = {0, 0, 0x80, 0x800, 0x10000};
  return kMinimums[length];
}

bool isContinuation(unsigned char byte) {
  return (byte & 0xC0) == 0x80;
}

} // namespace

std::string_view sanitizeUtf8(const char *data, std::size_t length) {
  std::size_t good = 0;
  while (good < length) {
    const unsigned char lead = static_cast<unsigned char>(data[good]);
    const unsigned length_ = sequenceLength(lead);
    if (length_ == 0 || good + length_ > length)
      break;
    std::size_t codePoint =
        lead & (length_ == 1 ? 0x7F : (0xFF >> (length_ + 1)));
    bool wellFormed = true;
    for (unsigned i = 1; i < length_; ++i) {
      const unsigned char next = static_cast<unsigned char>(data[good + i]);
      if (!isContinuation(next)) {
        wellFormed = false;
        break;
      }
      codePoint = (codePoint << 6) | (next & 0x3F);
    }
    if (!wellFormed || codePoint < minimumCodePoint(length_) ||
        (codePoint >= 0xD800 && codePoint <= 0xDFFF) || codePoint > 0x10FFFF)
      break;
    good += length_;
  }
  return std::string_view(data, good);
}

std::string_view sanitizeUtf8(const char *data) {
  return data ? sanitizeUtf8(data, std::strlen(data)) : std::string_view();
}

int countCharacters(std::string_view utf8) {
  int characters = 0;
  while (!utf8.empty()) {
    const unsigned length_ =
        sequenceLength(static_cast<unsigned char>(utf8.front()));
    if (length_ == 0 || length_ > utf8.size())
      break;
    for (unsigned i = 1; i < length_; ++i) {
      if (!isContinuation(static_cast<unsigned char>(utf8[i])))
        return characters;
    }
    utf8.remove_prefix(length_);
    ++characters;
  }
  return characters;
}

} // namespace framework::text
