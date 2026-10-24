#pragma once

// consolepage::ConsoleSelection - the console's text selection, kept out of
// the page translation unit: an ordered row/offset range over the stored row
// texts with the bookkeeping the drag, trim and copy paths need. Pure data and
// arithmetic - no DOM, no api table, no page state.

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace consolepage {

struct SelectionPoint {
  int Row = -1;
  std::size_t Offset = 0;
};

// Ordered row/offset range over the stored row texts; pure bookkeeping.
struct ConsoleSelection {
  SelectionPoint Anchor;
  SelectionPoint Focus;

  void begin(int row, std::size_t offset) {
    Anchor = SelectionPoint{row, offset};
    Focus = Anchor;
  }
  void extend(int row, std::size_t offset) {
    Focus = SelectionPoint{row, offset};
  }
  void clear() {
    Anchor = SelectionPoint{};
    Focus = SelectionPoint{};
  }
  bool hasAnchor() const {
    return Anchor.Row >= 0;
  }
  bool empty() const {
    return Anchor.Row < 0 || Focus.Row < 0 ||
           (Anchor.Row == Focus.Row && Anchor.Offset == Focus.Offset);
  }
  static bool before(const SelectionPoint &a, const SelectionPoint &b) {
    return a.Row < b.Row || (a.Row == b.Row && a.Offset < b.Offset);
  }
  SelectionPoint start() const {
    return before(Anchor, Focus) ? Anchor : Focus;
  }
  SelectionPoint end() const {
    return before(Anchor, Focus) ? Focus : Anchor;
  }
  // The first `count` rows were trimmed from the view.
  void trimFront(int count) {
    for (SelectionPoint *point : {&Anchor, &Focus}) {
      if (point->Row < 0)
        continue;
      point->Row -= count;
      if (point->Row < 0) {
        point->Row = 0;
        point->Offset = 0;
      }
    }
  }
  bool rowRange(int row, std::size_t rowBytes, std::size_t &beginOut,
                std::size_t &endOut) const {
    if (!hasAnchor() || row < start().Row || row > end().Row)
      return false;
    beginOut = 0;
    endOut = rowBytes;
    if (row == start().Row)
      beginOut = std::min(start().Offset, rowBytes);
    if (row == end().Row)
      endOut = std::min(end().Offset, rowBytes);
    return beginOut < endOut;
  }
  std::string text(const std::vector<std::string> &rows) const {
    if (empty())
      return {};
    std::string out;
    for (int row = start().Row; row <= end().Row; ++row) {
      std::size_t begin = 0;
      std::size_t end = 0;
      const std::size_t rowBytes = row < static_cast<int>(rows.size())
                                       ? rows[static_cast<size_t>(row)].size()
                                       : 0;
      if (!rowRange(row, rowBytes, begin, end))
        continue;
      if (!out.empty())
        out.push_back('\n');
      out += rows[static_cast<size_t>(row)].substr(begin, end - begin);
    }
    return out;
  }
};

} // namespace consolepage
