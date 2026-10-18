#pragma once

// framework::DocumentLoadGuard - the document-load failure detector (T21).
//
// RmlUi's XML parser is lenient: a syntactically broken file (unclosed tags,
// truncated markup) still produces a PARTIALLY parsed document, reported only
// as an "XML parse error" warning - LoadDocument returns non-null. Treating
// that as success is exactly the black-locked-screen failure the strict
// takeover forbids (live-caught in the T21a fault matrix: a corrupted
// mainmenu.rml presented an empty black page with no exit).
//
// The guard gives InteractiveDocument::load a synchronous "did anything go
// wrong while THIS document was being parsed" signal: AbcSystemInterface
// (the registered Rml::SystemInterface) funnels every Rml log message here,
// load() brackets its LoadDocument call with begin()/failedSince(), and a
// failure redirects the load into the built-in emergency path.
//
// What counts as failure (AbcSystemInterface routes by Rml::Log::Type):
//   - LT_ERROR / LT_ASSERT: any Rml-level error during the load window;
//   - LT_WARNING containing "XML parse error" (the parser's warning text,
//     RmlUi submodule pin: Source/Core/BaseXMLParser.cpp:172/451 - re-diff on
//     submodule bumps).
// Ordinary warnings (a missing image, a CSS property fallback) do NOT fail
// the load: those degrade visually while the page itself stays operable.
//
// Header-only: two counters and inline reads, no state ownership.

#include <cstdint>
#include <cstring>

namespace framework {
namespace documentload {

// Bumped by the Rml log seam. Plain ints, single-threaded (the Rml context
// and every load run on the main thread by construction).
inline uint64_t &failureCounter() {
  static uint64_t counter = 0;
  return counter;
}

// Called from AbcSystemInterface::LogMessage. `type` is Rml::Log::Type
// (LT_ERROR=1, LT_ASSERT=2, LT_WARNING=3 - kept as int to avoid an RmlUi
// include here).
inline void noteRmlLog(int type, const char *message) {
  constexpr int kLtError = 1;
  constexpr int kLtAssert = 2;
  constexpr int kLtWarning = 3;
  if (type == kLtError || type == kLtAssert) {
    ++failureCounter();
    return;
  }
  if (type == kLtWarning && message && std::strstr(message, "XML parse error"))
    ++failureCounter();
}

// Bracket one synchronous load: mark = begin(); ...; failedSince(mark).
inline uint64_t begin() {
  return failureCounter();
}

inline bool failedSince(uint64_t mark) {
  return failureCounter() != mark;
}

} // namespace documentload
} // namespace framework
