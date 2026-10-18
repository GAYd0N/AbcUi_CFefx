// panels::console::ConsoleSelection implementation - see ConsoleSelection.h.
#include "ConsoleSelection.h"

#include <algorithm>

namespace panels::console {

namespace {

bool before(const ConsoleSelection::Point &a,
            const ConsoleSelection::Point &b) {
  return a.Row < b.Row || (a.Row == b.Row && a.Offset < b.Offset);
}

std::size_t clampedOffset(std::size_t offset, std::size_t size) {
  return std::min(offset, size);
}

} // namespace

void ConsoleSelection::begin(int row, std::size_t offset) {
  Anchor = Point{row, offset};
  Focus = Anchor;
}

void ConsoleSelection::extend(int row, std::size_t offset) {
  if (Anchor.Row < 0) {
    begin(row, offset);
    return;
  }
  Focus = Point{row, offset};
}

void ConsoleSelection::clear() {
  Anchor = Point{};
  Focus = Point{};
}

ConsoleSelection::Point ConsoleSelection::start() const {
  return before(Focus, Anchor) ? Focus : Anchor;
}

ConsoleSelection::Point ConsoleSelection::end() const {
  return before(Focus, Anchor) ? Anchor : Focus;
}

void ConsoleSelection::trimFront(int count) {
  if (count <= 0 || Anchor.Row < 0 || Focus.Row < 0)
    return;
  const auto shift = [count](Point &point) {
    point.Row -= count;
    if (point.Row < 0) {
      // The endpoint was in a trimmed row: keep the surviving tail from
      // the very beginning of the new first row.
      point.Row = 0;
      point.Offset = 0;
    }
  };
  shift(Anchor);
  shift(Focus);
}

bool ConsoleSelection::rowRange(int row, std::size_t rowBytes,
                                std::size_t &rangeBegin,
                                std::size_t &rangeEnd) const {
  if (empty())
    return false;
  const Point first = start();
  const Point last = end();
  if (row < first.Row || row > last.Row)
    return false;

  if (first.Row == last.Row) {
    rangeBegin = clampedOffset(first.Offset, rowBytes);
    rangeEnd = clampedOffset(last.Offset, rowBytes);
  } else if (row == first.Row) {
    rangeBegin = clampedOffset(first.Offset, rowBytes);
    rangeEnd = rowBytes;
  } else if (row == last.Row) {
    rangeBegin = 0;
    rangeEnd = clampedOffset(last.Offset, rowBytes);
  } else {
    rangeBegin = 0;
    rangeEnd = rowBytes;
  }
  return rangeBegin < rangeEnd;
}

std::string ConsoleSelection::text(const std::vector<std::string> &rows) const {
  if (empty() || rows.empty())
    return {};
  const Point first = start();
  const Point last = end();
  if (first.Row >= static_cast<int>(rows.size()))
    return {};

  std::string out;
  const int lastRow = std::min(last.Row, static_cast<int>(rows.size()) - 1);
  for (int row = first.Row; row <= lastRow; ++row) {
    if (row > first.Row)
      out.push_back('\n');
    const std::string &text = rows[static_cast<std::size_t>(row)];
    std::size_t begin = 0;
    std::size_t end = 0;
    if (rowRange(row, text.size(), begin, end))
      out.append(text, begin, end - begin);
  }
  return out;
}

} // namespace panels::console
