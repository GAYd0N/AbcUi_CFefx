#pragma once

// panels::console::ConsoleSelection - the console's text-selection model.
//
// Engine/Rml-free on purpose: row/offset bookkeeping, trim adjustment and
// copy text are pure string logic and link into the offline acceptance test
// without a context. The page maps pointer positions to (row, byte offset)
// and renders the highlight; this class owns only the ordered range.
//
// Offsets are UTF-8 byte offsets into the stored row text. The page derives
// them from laid-out text runs, so they always land on character boundaries.

#include <cstddef>
#include <string>
#include <vector>

namespace panels::console {

class ConsoleSelection {
public:
  struct Point {
    int Row = -1;
    std::size_t Offset = 0;
  };

  // Starts a new selection at `point` (a caret with no selected text).
  void begin(int row, std::size_t offset);
  // Moves the free end while the anchor stays put.
  void extend(int row, std::size_t offset);
  void clear();

  // A pointer press established an anchor (the range may still be empty).
  bool hasAnchor() const {
    return Anchor.Row >= 0;
  }
  bool empty() const {
    return Anchor.Row < 0 || Focus.Row < 0 ||
           (Anchor.Row == Focus.Row && Anchor.Offset == Focus.Offset);
  }

  // Ordered document endpoints (start <= end).
  Point start() const;
  Point end() const;

  // The first `count` rows were trimmed from the view. Endpoints move with
  // the remaining rows; an endpoint inside a trimmed row clamps to the new
  // first row's start, so a selection that still covers surviving rows
  // stays useful. With no rows left the page clears the selection itself.
  void trimFront(int count);

  // Selected half-open byte range inside `row`, clamped to `rowBytes`.
  // Returns false when the intersection is empty.
  bool rowRange(int row, std::size_t rowBytes, std::size_t &begin,
                std::size_t &end) const;

  // Copy payload: covered rows joined with '\n'. Rows not present in
  // `rows` are treated as empty (the DOM and the model are kept in step,
  // this is only a defensive clamp).
  std::string text(const std::vector<std::string> &rows) const;

private:
  Point Anchor;
  Point Focus;
};

} // namespace panels::console
