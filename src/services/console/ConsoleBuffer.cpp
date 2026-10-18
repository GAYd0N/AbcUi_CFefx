// services::console::ConsoleBuffer / ConsoleCapture implementation - see
// ConsoleBuffer.h.
#include "ConsoleBuffer.h"

#include <algorithm>

namespace services::console {

namespace {

// Sequence length from a UTF-8 lead byte (RFC 3629); 0 = not a lead byte.
// Local on purpose: the general sanitizer (framework::text::sanitizeUtf8)
// rejects a whole buffer at the first malformed sequence, while the byte-cap
// cut only needs the tail check - a partial sequence at the very end of a
// filled window.
unsigned utf8SequenceLength(unsigned char lead) {
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

} // namespace

void ConsoleBuffer::pushText(const char *text, std::size_t length,
                             bool developer) {
  std::lock_guard<std::mutex> lock(Mutex);
  if (Stopped) {
    ++Stats.RejectedAfterShutdown;
    return;
  }
  ++Stats.Fragments;
  if (!text || length == 0)
    return;
  for (std::size_t i = 0; i < length; ++i) {
    const char byte = text[i];
    if (byte == '\r')
      continue; // CRLF/CR noise never reaches the line text
    if (byte == '\n') {
      finalizeLineLocked();
      continue;
    }
    if (PendingTruncated) {
      // Past the byte cap: everything up to the newline is dropped and
      // counted (the line stays as its kept prefix).
      ++Stats.DroppedBytes;
      continue;
    }
    if (Pending.size() >= kMaxLineBytes) {
      // First byte past the cap: cut a partial UTF-8 sequence off the
      // tail, then start discarding the rest of this line.
      trimPartialSequenceLocked();
      PendingTruncated = true;
      ++Stats.DroppedBytes;
      continue;
    }
    Pending.push_back(byte);
    if (developer)
      PendingDeveloper = true;
  }
}

void ConsoleBuffer::finalizeLineLocked() {
  ConsoleLine line;
  line.Text = std::move(Pending);
  Pending.clear();
  line.Sequence = ++SequenceCounter;
  line.Developer = PendingDeveloper;
  line.Truncated = PendingTruncated;
  PendingDeveloper = false;
  PendingTruncated = false;
  if (Lines.size() >= kMaxLines) {
    Lines.pop_front();
    ++Stats.DroppedLines;
  }
  Lines.push_back(std::move(line));
}

void ConsoleBuffer::trimPartialSequenceLocked() {
  // A partial UTF-8 sequence can begin at most 3 bytes before the cut.
  for (std::size_t back = 1; back <= 3 && back <= Pending.size(); ++back) {
    const std::size_t pos = Pending.size() - back;
    const unsigned char lead = static_cast<unsigned char>(Pending[pos]);
    if (lead < 0x80)
      return; // ASCII tail: nothing to cut
    if ((lead & 0xC0) == 0x80)
      continue; // continuation byte: its lead sits earlier
    if (utf8SequenceLength(lead) > back) {
      Stats.DroppedBytes += Pending.size() - pos;
      Pending.resize(pos);
    }
    return;
  }
}

void ConsoleBuffer::clear() {
  std::lock_guard<std::mutex> lock(Mutex);
  Lines.clear();
  Pending.clear();
  PendingDeveloper = false;
  PendingTruncated = false;
  ++GenerationCounter;
}

void ConsoleBuffer::shutdown() {
  std::lock_guard<std::mutex> lock(Mutex);
  if (Stopped)
    return;
  Stopped = true;
  Lines.clear();
  Pending.clear();
  PendingDeveloper = false;
  PendingTruncated = false;
  ++GenerationCounter;
}

bool ConsoleBuffer::stopped() const {
  std::lock_guard<std::mutex> lock(Mutex);
  return Stopped;
}

std::uint64_t ConsoleBuffer::generation() const {
  std::lock_guard<std::mutex> lock(Mutex);
  return GenerationCounter;
}

std::uint64_t ConsoleBuffer::latestSequence() const {
  std::lock_guard<std::mutex> lock(Mutex);
  return SequenceCounter;
}

std::size_t ConsoleBuffer::lineCount() const {
  std::lock_guard<std::mutex> lock(Mutex);
  return Lines.size();
}

ConsoleBufferStats ConsoleBuffer::stats() const {
  std::lock_guard<std::mutex> lock(Mutex);
  return Stats;
}

std::size_t ConsoleBuffer::copySince(std::uint64_t since,
                                     std::vector<ConsoleLine> &out,
                                     std::size_t max) const {
  std::lock_guard<std::mutex> lock(Mutex);
  out.clear();
  if (max == 0)
    return 0;
  std::size_t first = 0;
  while (first < Lines.size() && Lines[first].Sequence <= since)
    ++first;
  const std::size_t available = Lines.size() - first;
  if (available > max)
    first = Lines.size() - max; // bounded cache pages out its newest tail
  out.assign(Lines.begin() + static_cast<std::ptrdiff_t>(first), Lines.end());
  return out.size();
}

// --- ConsoleCapture --------------------------------------------------------

void ConsoleCapture::onPrintf(const void *source, const char *data,
                              std::size_t length, bool post) {
  capture(source, data, length, post, kPrintf);
}

void ConsoleCapture::onDPrintf(const void *source, const char *data,
                               std::size_t length, bool post) {
  capture(source, data, length, post, kDPrintf);
}

void ConsoleCapture::capture(const void *source, const char *data,
                             std::size_t length, bool post, std::uint8_t kind) {
  if (Detached)
    return;
  if (!source) {
    // No identity to dedup on: store the pre phase only (the post is an
    // echo that cannot be proven distinct).
    if (!post)
      Buffer.pushText(data, length, kind == kDPrintf);
    return;
  }
  const bool developer = kind == kDPrintf;
  if (!post) {
    if (Pending.size() >= kMaxPending)
      Pending.erase(Pending.begin()); // oldest stale key never blocks a new pre
    Pending.push_back(PendingKey{source, kind, length});
    Buffer.pushText(data, length, developer);
    return;
  }
  const PendingKey key{source, kind, length};
  const auto it = std::find_if(
      Pending.begin(), Pending.end(), [&key](const PendingKey &entry) {
        return entry.Source == key.Source && entry.Kind == key.Kind &&
               entry.Length == key.Length;
      });
  if (it != Pending.end()) {
    Pending.erase(it); // the post echo of a message already stored
    return;
  }
  Buffer.pushText(data, length, developer);
}

void ConsoleCapture::onClear(bool post) {
  if (Detached)
    return;
  if (!post) {
    Buffer.clear();
    ClearPreSeen = true;
    return;
  }
  if (ClearPreSeen) {
    ClearPreSeen = false; // the post echo of the same Clear dispatch
    return;
  }
  Buffer.clear(); // pre was superseded upstream; this post still clears once
}

void ConsoleCapture::detach() {
  Detached = true;
  Pending.clear();
  ClearPreSeen = false;
}

void ConsoleCapture::attach() {
  Detached = false;
  Pending.clear();
  ClearPreSeen = false;
}

// --- singletons -------------------------------------------------------------

ConsoleBuffer &consoleBuffer() {
  static ConsoleBuffer Buffer;
  return Buffer;
}

ConsoleCapture &consoleCapture() {
  static ConsoleCapture Capture(consoleBuffer());
  return Capture;
}

} // namespace services::console
