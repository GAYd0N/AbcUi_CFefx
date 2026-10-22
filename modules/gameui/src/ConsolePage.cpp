// The replacement console page (moved from panels::console). A top-band
// overlay document presenting itself from the menu state's consoleOpen
// fact: it renders the bounded engine output mirror (the sdk consolepage
// section's copySince window) and drives the user input loop against the
// same section's submission channel, history and completion catalog.
//
// Hard rules carried over from the T16 record:
//   - Output is ALWAYS text: every engine line is escaped before it reaches
//     the DOM; markup in engine output can never become structure.
//   - The live projection is bounded: the first render of a view takes only
//     the most recent window, later renders append only new lines, and the
//     DOM holds at most the row cap (oldest trimmed).
//   - Scroll is honest: a reader parked above the bottom is never yanked
//     down by appends; a viewer at the bottom follows the tail.
//   - The same laid-out lines drive mouse selection: drag over rows,
//     Ctrl+C copies the selected text, and a right-click opens the Copy
//     menu (visible output when nothing is selected).
//   - The editor is single-line: Enter submits through the section, Up/Down
//     walk the history, Tab completes through the catalog. Every submitted
//     command line is mirrored into the scrollback as "] <command>" before
//     execution.
//   - A multi-line paste NEVER executes silently: Ctrl+V with newline-
//     bearing clipboard text opens the confirm pane; only its explicit
//     Confirm submits (one validated command per line), Cancel restores.
//   - ESC layering: a press the page consumed on one of its own layers
//     (context menu / confirm pane / completion list) is claimed through
//     the section, so the menu owner's routed ESC closes exactly one layer.
#include "PageShared.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace consolepage {
namespace {

// Frozen projection caps (the T16 record).
constexpr int kInitialLines = 200;  // first render of a view
constexpr int kMaxDomLines = 300;   // DOM row cap
constexpr int kMaxSuggestions = 10; // completion dropdown row cap

constexpr const char *kDocumentName = "menuconsole";
constexpr const char *kDocumentPath =
    "mod://abcui.gameui/gameui/rml/console.rml";

AbcUiDocument *Page = nullptr;
AbcUiElement *BoundRoot = nullptr;
uint64_t ClickBinding = 0;
uint64_t MousedownBinding = 0;
uint64_t DragBinding = 0;
uint64_t KeydownBinding = 0;
uint64_t KeyupBinding = 0;
uint64_t BlurBinding = 0;
AbcUiSubscription StateSubscription = 0;
AbcUiSubscription PaintSubscription = 0;

bool Prepared = false;

// --- selection model (panels::console::ConsoleSelection, ported) -----------

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

// --- page state -------------------------------------------------------------

ConsoleSelection Selection;
// The visible rows' source text and their appended handles, index-aligned
// (the DOM can be trimmed; these mirrors trim with it).
std::vector<std::string> RowTexts;
std::vector<AbcUiElement *> RowNodes;
bool SelectionDirty = false;
bool ContextMenuOpen = false;
float SelectionScrollTop = -1.0f;
float SelectionScrollHeight = -1.0f;
float SelectionWidth = -1.0f;

// Projection cursor: the last projected buffer sequence and the buffer
// generation the DOM view belongs to (a Clear bump reseeds the view).
uint64_t ProjectedSequence = 0;
uint64_t ProjectedGeneration = 0;
// Tail follow across the live frame ordering: an append arms it, the next
// render (fresh layout) applies it unless the reader scrolled up.
bool FollowPending = false;
float FollowFromScrollTop = 0.0f;

// The pending multi-line text (confirm pane); empty = pane closed.
std::string PendingText;
// Completion state for the current editor content.
std::vector<std::string> Suggestions;
int SuggestionIndex = -1;
// An OPEN Tab completion session: the first Tab lists the candidates
// (index 0 highlighted, editor extended to the common prefix); every later
// Tab cycles index and drops that candidate into the editor. Never
// re-filtered from its own completed text - only a real edit, Esc,
// Enter/click or a blur ends it.
bool CompletionSession = false;
// History walk cursor: -1 = live editing, else an offset from the tail of
// the history; the draft restores on the way back down.
int HistoryCursor = -1;
std::string HistoryDraft;

AbcUiElement *node(const char *id) {
  return Page ? Api->ui->querySelector(Page, id) : nullptr;
}

void setInnerRml(const char *id, const std::string &rml) {
  if (AbcUiElement *element = node(id))
    Api->ui->elementSetInnerRML(element, rml.c_str());
}

void setDisplay(const char *id, bool visible) {
  if (AbcUiElement *element = node(id))
    Api->ui->elementSetProperty(element, "display", visible ? "block" : "none");
}

std::string inputValue(const char *id) {
  AbcUiElement *control = node(id);
  if (!control)
    return {};
  const int needed = Api->ui->elementValue(control, nullptr, 0);
  if (needed <= 0)
    return {};
  std::string out(static_cast<size_t>(needed) + 1, '\0');
  Api->ui->elementValue(control, out.data(), static_cast<uint32_t>(out.size()));
  out.resize(static_cast<size_t>(needed));
  return out;
}

void setInputValue(const char *id, const std::string &value) {
  if (AbcUiElement *control = node(id))
    Api->ui->elementSetValue(control, value.c_str());
}

bool containsNewline(const std::string &text) {
  return text.find('\n') != std::string::npos ||
         text.find('\r') != std::string::npos;
}

bool insideNode(AbcUiElement *nodeElement, AbcUiElement *ancestor) {
  for (AbcUiElement *it = nodeElement; it; it = Api->ui->elementParent(it)) {
    if (it == ancestor)
      return true;
  }
  return false;
}

// A point belongs to the output only when it is inside a row's text; the
// history's own blank area also starts a selection, but its scrollbar (a
// child with drag=block) must keep owning the pointer.
bool insideConsoleRow(AbcUiElement *target, AbcUiElement *history) {
  for (AbcUiElement *it = target; it; it = Api->ui->elementParent(it)) {
    if (Api->ui->elementClassSet(it, "console-line"))
      return true;
    if (it == history)
      return false;
  }
  return false;
}

// Next UTF-8 character boundary at or after `at`.
std::size_t nextBoundary(const std::string &text, std::size_t at) {
  if (at >= text.size())
    return text.size();
  ++at;
  while (at < text.size() && (text[at] & 0xC0) == 0x80)
    ++at;
  return at;
}

// The laid-out lines of one row (positions are relative to the text
// origin the same call reports).
int rowLines(AbcUiElement *row, std::vector<AbcUiTextLine> &lines,
             float &lineHeight, float &originX, float &originY) {
  AbcUiTextLine buffer[64];
  const int count = Api->ui->elementTextLayout(row, buffer, 64, &lineHeight,
                                               &originX, &originY);
  if (count > 0) {
    lines.assign(buffer, buffer + std::min<int>(count, 64));
  } else {
    lines.clear();
  }
  return count;
}

// Maps a horizontal offset inside one laid-out line to the nearest UTF-8
// character boundary. A click in the left/right half of a glyph snaps to
// the matching side, matching the feel of a native text selection.
std::size_t offsetForX(AbcUiElement *row, const std::string &line, float x) {
  if (x <= 0.0f || line.empty())
    return 0;
  std::size_t previous = 0;
  int previousWidth = 0;
  for (std::size_t current = nextBoundary(line, 0);;
       current = nextBoundary(line, current)) {
    const int currentWidth = Api->ui->elementTextPrefixWidth(
        row, line.data(), static_cast<uint32_t>(current));
    if (static_cast<float>(currentWidth) >= x) {
      if (x - static_cast<float>(previousWidth) <=
          static_cast<float>(currentWidth) - x)
        return previous;
      return current;
    }
    previous = current;
    previousWidth = currentWidth;
    if (current >= line.size())
      break;
  }
  return line.size();
}

struct Caret {
  int Row = -1;
  std::size_t Offset = 0;
};

Caret caretFromPoint(float pointX, float pointY) {
  AbcUiElement *history = node("console-history");
  if (!history || RowTexts.empty())
    return {};
  const int32_t childCount = Api->ui->elementChildCount(history);
  const int rowCount =
      std::min<int>(childCount, static_cast<int>(RowTexts.size()));
  if (rowCount <= 0)
    return {};

  int row = rowCount - 1;
  for (int i = 0; i < rowCount; ++i) {
    AbcUiElement *candidate = Api->ui->elementChild(history, i);
    if (!candidate)
      continue;
    float width = 0.0f;
    float height = 0.0f;
    float top = 0.0f;
    Api->ui->elementAbsoluteOffset(candidate, nullptr, &top);
    Api->ui->elementOffsetSize(candidate, &width, &height);
    if (pointY < top + height) {
      row = i;
      break;
    }
  }

  AbcUiElement *rowElement = Api->ui->elementChild(history, row);
  if (!rowElement)
    return Caret{row, 0};
  std::vector<AbcUiTextLine> lines;
  float lineHeight = 0.0f;
  float originX = 0.0f;
  float originY = 0.0f;
  if (rowLines(rowElement, lines, lineHeight, originX, originY) <= 0 ||
      lines.empty())
    return Caret{row, 0};
  const float relativeY = pointY - originY;
  int lineIndex = 0;
  for (int i = 1; i < static_cast<int>(lines.size()); ++i) {
    if (relativeY < lines[static_cast<size_t>(i)].y)
      break;
    lineIndex = i;
  }
  const AbcUiTextLine &line = lines[static_cast<size_t>(lineIndex)];
  std::size_t lineStart = 0;
  for (int i = 0; i < lineIndex; ++i)
    lineStart += lines[static_cast<size_t>(i)].bytes;
  const float relativeX = pointX - originX - line.x;
  const std::string lineText =
      RowTexts[static_cast<size_t>(row)].substr(lineStart, line.bytes);
  const std::size_t local = offsetForX(rowElement, lineText, relativeX);
  const std::size_t offset =
      std::min(lineStart + local, RowTexts[static_cast<size_t>(row)].size());
  return Caret{row, offset};
}

// Parks the editor's caret at the end of its text: the widget's selection
// API takes CHARACTER offsets, so the real character count is the value
// that means "behind the text".
void parkCaretAtEnd() {
  AbcUiElement *editor = node("console-input");
  if (!editor)
    return;
  const std::string value = inputValue("console-input");
  int characters = 0;
  for (std::size_t at = 0; at < value.size(); at = nextBoundary(value, at))
    ++characters;
  Api->ui->elementSetSelection(editor, characters, characters);
}

// A completion replaces the editor text wholesale, so a live IME
// composition must be dropped first - its commit would otherwise be
// appended to the completed candidate instead of replacing the prefix.
void endEditorComposition() {
  if (Api->input->textComposing())
    Api->input->cancelComposition();
}

// Splits a confirmed multi-line text into executable command lines: CR is
// dropped, empty/whitespace-only lines are skipped (each line is its own
// validated submission).
std::vector<std::string> splitCommandLines(const std::string &text) {
  std::vector<std::string> lines;
  std::string current;
  const auto flush = [&]() {
    std::size_t begin = 0;
    while (begin < current.size() &&
           (current[begin] == ' ' || current[begin] == '\t'))
      ++begin;
    std::size_t end = current.size();
    while (end > begin && (current[end - 1] == ' ' || current[end - 1] == '\t'))
      --end;
    if (begin < end)
      lines.push_back(current.substr(begin, end - begin));
    current.clear();
  };
  for (char c : text) {
    if (c == '\r')
      continue;
    if (c == '\n') {
      flush();
      continue;
    }
    current.push_back(c);
  }
  flush();
  return lines;
}

// --- projection -------------------------------------------------------------

void appendLine(const std::string &text, bool developer, bool truncated) {
  AbcUiElement *history = node("console-history");
  if (!history)
    return;
  // Output is always text: escaped, never parsed as markup.
  const std::string shown = text + (truncated ? " …" : "");
  AbcUiElement *row = Api->ui->elementAppendRml(
      history, escapeText(shown).c_str(), "console-line");
  if (!row)
    return;
  if (developer)
    Api->ui->elementSetClass(row, "console-dev", 1);
  RowTexts.push_back(shown);
  RowNodes.push_back(row);
  SelectionDirty = true;
}

void trimDom() {
  AbcUiElement *history = node("console-history");
  if (!history)
    return;
  int removed = 0;
  while (Api->ui->elementChildCount(history) > kMaxDomLines &&
         !RowNodes.empty()) {
    if (RowNodes.front())
      Api->ui->elementRemove(RowNodes.front());
    RowNodes.erase(RowNodes.begin());
    ++removed;
  }
  if (removed > 0) {
    const std::size_t count =
        std::min(RowTexts.size(), static_cast<std::size_t>(removed));
    RowTexts.erase(RowTexts.begin(),
                   RowTexts.begin() + static_cast<std::ptrdiff_t>(count));
    Selection.trimFront(removed);
    if (RowTexts.empty())
      Selection.clear();
    SelectionDirty = true;
  }
}

void updateCountLine() {
  // "total: N" - the buffer's own line count (the DOM holds a capped
  // window of it). Plain text; the token resolves through the lang table.
  std::string label = translate("Console_Count");
  setInnerRml(
      "console-count",
      escapeText(label + " " + std::to_string(Api->consolepage->lineCount()))
          .c_str());
}

void refreshSelectionOverlay(bool force = false);

void render() {
  if (!Page || !Api->documents->visible(Page) || !Api->consolepage)
    return;
  AbcUiElement *history = node("console-history");
  if (!history)
    return;

  const uint64_t generation = Api->consolepage->generation();
  if (generation != ProjectedGeneration) {
    // Clear (or a rebuild): drop the whole view and start over from the
    // current tail window.
    ProjectedGeneration = generation;
    ProjectedSequence = 0;
    for (AbcUiElement *rowNode : RowNodes) {
      if (rowNode)
        Api->ui->elementRemove(rowNode);
    }
    RowNodes.clear();
    RowTexts.clear();
    Selection.clear();
    SelectionDirty = true;
  }

  if (ProjectedSequence == 0) {
    // First render of this view: only the recent window (bounded), never
    // the full backlog - the cache is a tail by design.
    uint64_t since = 0;
    const uint64_t latest = Api->consolepage->latestSequence();
    if (latest > static_cast<uint64_t>(kInitialLines))
      since = latest - static_cast<uint64_t>(kInitialLines);
    static std::vector<AbcUiConsoleLine> lines;
    lines.resize(static_cast<size_t>(kInitialLines));
    for (auto &line : lines)
      line.structSize = sizeof(line);
    const int count = Api->consolepage->copySince(
        since, lines.data(), static_cast<uint32_t>(kInitialLines));
    for (int i = 0; i < count; ++i)
      appendLine(lines[i].text, lines[i].developer != 0,
                 lines[i].truncated != 0);
    ProjectedSequence = Api->consolepage->latestSequence();
    trimDom();
    // A fresh view starts at the tail.
    Api->ui->elementSetScrollTop(history,
                                 Api->ui->elementScrollHeight(history));
    updateCountLine();
    refreshSelectionOverlay();
    return;
  }

  const uint64_t latest = Api->consolepage->latestSequence();
  if (latest <= ProjectedSequence) {
    // The pending tail follow (see the append branch): the metrics are
    // current now, so the one-pass-behind scroll can land exactly at the
    // bottom - unless the reader scrolled up since it was armed.
    if (FollowPending && !Selection.hasAnchor()) {
      FollowPending = false;
      if (Api->ui->elementScrollTop(history) + 1.0f >= FollowFromScrollTop)
        Api->ui->elementSetScrollTop(history,
                                     Api->ui->elementScrollHeight(history));
    }
    updateCountLine();
    refreshSelectionOverlay();
    return; // no new lines
  }

  // Incremental append: only lines newer than the projected cursor.
  const float scrollTopBefore = Api->ui->elementScrollTop(history);
  const float scrollHeightBefore = Api->ui->elementScrollHeight(history);
  const float clientHeight = Api->ui->elementClientHeight(history);
  // A selection pins the view: appends during a drag (or while selected
  // text waits to be copied) must never yank the range off screen.
  const bool atBottom =
      !Selection.hasAnchor() &&
      scrollTopBefore + clientHeight >= scrollHeightBefore - 1.0f;

  static std::vector<AbcUiConsoleLine> window;
  window.resize(static_cast<size_t>(kMaxDomLines));
  for (auto &line : window)
    line.structSize = sizeof(line);
  // The consumer window is bounded too: a long gap (the page was hidden
  // while output burst) pages from the newest tail, never from an
  // unbounded backlog.
  const int count = Api->consolepage->copySince(
      ProjectedSequence, window.data(), static_cast<uint32_t>(kMaxDomLines));
  for (int i = 0; i < count; ++i)
    appendLine(window[i].text, window[i].developer != 0,
               window[i].truncated != 0);
  ProjectedSequence = Api->consolepage->latestSequence();
  trimDom();
  if (atBottom) {
    // The scroll set here lands on the CURRENT layout only when the
    // context already laid the appended rows out. Live frames render
    // before Context::Update, so the bounds are one layout pass behind:
    // the follow is re-armed and re-applied on the next render (one frame
    // later, imperceptible) against fresh metrics.
    Api->ui->elementSetScrollTop(history,
                                 Api->ui->elementScrollHeight(history));
    FollowPending = true;
    FollowFromScrollTop = scrollTopBefore;
  } else {
    FollowPending = false;
    // The reader is parked above the bottom: keep the visible content
    // stable. Trimming the oldest rows shrinks the scroll height; the
    // scroll top compensates by the same amount.
    const float delta =
        scrollHeightBefore - Api->ui->elementScrollHeight(history);
    float target = scrollTopBefore - delta;
    if (target < 0.0f)
      target = 0.0f;
    Api->ui->elementSetScrollTop(history, target);
  }
  updateCountLine();
  refreshSelectionOverlay();
}

void refreshSelectionOverlay(bool force) {
  AbcUiElement *history = node("console-history");
  AbcUiElement *layer = node("console-selection");
  if (!history || !layer)
    return;
  if (Selection.empty()) {
    if (Api->ui->elementChildCount(layer) != 0) {
      for (int32_t i = Api->ui->elementChildCount(layer) - 1; i >= 0; --i) {
        if (AbcUiElement *child = Api->ui->elementChild(layer, i))
          Api->ui->elementRemove(child);
      }
    }
    SelectionDirty = false;
    return;
  }
  const float scrollTop = Api->ui->elementScrollTop(history);
  const float scrollHeight = Api->ui->elementScrollHeight(history);
  float width = 0.0f;
  float layerHeight = 0.0f;
  Api->ui->elementOffsetSize(history, &width, nullptr);
  if (!force && !SelectionDirty && scrollTop == SelectionScrollTop &&
      scrollHeight == SelectionScrollHeight && width == SelectionWidth)
    return;
  SelectionDirty = false;
  SelectionScrollTop = scrollTop;
  SelectionScrollHeight = scrollHeight;
  SelectionWidth = width;
  for (int32_t i = Api->ui->elementChildCount(layer) - 1; i >= 0; --i) {
    if (AbcUiElement *child = Api->ui->elementChild(layer, i))
      Api->ui->elementRemove(child);
  }

  const SelectionPoint first = Selection.start();
  const SelectionPoint last = Selection.end();
  const int32_t childCount = Api->ui->elementChildCount(history);
  const int rowCount =
      std::min<int>(childCount, static_cast<int>(RowTexts.size()));
  float layerOriginX = 0.0f;
  float layerOriginY = 0.0f;
  Api->ui->elementAbsoluteOffset(layer, &layerOriginX, &layerOriginY);
  Api->ui->elementOffsetSize(layer, nullptr, &layerHeight);
  for (int row = first.Row; row <= last.Row && row < rowCount; ++row) {
    if (row < 0)
      continue;
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!Selection.rowRange(row, RowTexts[static_cast<size_t>(row)].size(),
                            begin, end))
      continue;
    AbcUiElement *rowElement = Api->ui->elementChild(history, row);
    if (!rowElement)
      continue;
    std::vector<AbcUiTextLine> lines;
    float lineHeight = 0.0f;
    float textOriginX = 0.0f;
    float textOriginY = 0.0f;
    if (rowLines(rowElement, lines, lineHeight, textOriginX, textOriginY) <= 0)
      continue;
    std::size_t lineStart = 0;
    for (const AbcUiTextLine &line : lines) {
      const std::size_t currentStart = lineStart;
      const std::size_t currentEnd = currentStart + line.bytes;
      const std::size_t selectedBegin = std::max(begin, currentStart);
      const std::size_t selectedEnd = std::min(end, currentEnd);
      lineStart = currentEnd;
      if (selectedBegin >= selectedEnd)
        continue;
      const std::string rowText = RowTexts[static_cast<size_t>(row)];
      const float x =
          textOriginX + line.x +
          static_cast<float>(Api->ui->elementTextPrefixWidth(
              rowElement, rowText.data() + currentStart,
              static_cast<uint32_t>(selectedBegin - currentStart))) -
          layerOriginX;
      const float right =
          textOriginX + line.x +
          static_cast<float>(Api->ui->elementTextPrefixWidth(
              rowElement, rowText.data() + currentStart,
              static_cast<uint32_t>(selectedEnd - currentStart))) -
          layerOriginX;
      const float y = textOriginY + line.y - layerOriginY;
      if (right <= x || y + lineHeight <= 0.0f || y >= layerHeight)
        continue;
      char style[192];
      std::snprintf(style, sizeof(style),
                    "left:%.2fpx; top:%.2fpx; width:%.2fpx; height:%.2fpx;", x,
                    y, right - x, lineHeight);
      AbcUiElement *rectangle =
          Api->ui->elementAppendRml(layer, "", "console-selection-rect");
      if (!rectangle)
        continue;
      Api->ui->elementSetAttribute(rectangle, "style", style);
    }
  }
}

// --- interaction -------------------------------------------------------------

void clearSelection() {
  if (!Selection.hasAnchor())
    return;
  Selection.clear();
  SelectionDirty = true;
  refreshSelectionOverlay(true);
}

void copySelection() {
  if (Selection.empty())
    return;
  const std::string text = Selection.text(RowTexts);
  if (!text.empty())
    Api->shell->copyText(text.c_str());
}

void copyOutput() {
  std::string text;
  if (!Selection.empty()) {
    text = Selection.text(RowTexts);
  } else {
    for (const std::string &row : RowTexts) {
      if (!text.empty())
        text.push_back('\n');
      text += row;
    }
  }
  if (!text.empty())
    Api->shell->copyText(text.c_str());
}

void closeContextMenu();

void openContextMenu(float pointX, float pointY) {
  AbcUiElement *menu = node("console-context-menu");
  if (!menu || !Page)
    return;
  // The menu uses viewport pixel coordinates (the same space as the mouse
  // event); clamp it so a right click near an edge still shows the whole
  // entry. Its size is known from console.rcss (86dp min width, one 20dp
  // row).
  float scale = 1.0f;
  Api->ui->documentScale(Page, &scale);
  int docWidth = 0;
  int docHeight = 0;
  Api->ui->documentSize(Page, &docWidth, &docHeight);
  const float menuWidth = 94.0f * scale;
  const float menuHeight = 31.0f * scale;
  float x = std::clamp(
      pointX, 0.0f, std::max(0.0f, static_cast<float>(docWidth) - menuWidth));
  float y = std::clamp(
      pointY, 0.0f, std::max(0.0f, static_cast<float>(docHeight) - menuHeight));
  char left[40];
  char top[40];
  std::snprintf(left, sizeof(left), "%.0fpx", x);
  std::snprintf(top, sizeof(top), "%.0fpx", y);
  Api->ui->elementSetProperty(menu, "left", left);
  Api->ui->elementSetProperty(menu, "top", top);
  setDisplay("console-context-menu", true);
  ContextMenuOpen = true;
}

void closeContextMenu() {
  ContextMenuOpen = false;
  setDisplay("console-context-menu", false);
}

// --- completion
// ---------------------------------------------------------------

void hideCompletion() {
  Suggestions.clear();
  SuggestionIndex = -1;
  CompletionSession = false;
  setDisplay("console-suggestions", false);
}

void submitFromEditor();

// Applies a candidate to the editor and closes the list. `run` executes it
// (the Tab-session Enter path); a click only completes the text.
void applyCompletion(int index, bool run = false) {
  if (index < 0 || index >= static_cast<int>(Suggestions.size()))
    return;
  endEditorComposition();
  setInputValue("console-input", Suggestions[static_cast<size_t>(index)]);
  // SetValue() only writes the value attribute; the widget's caret stays
  // where the typed prefix left it. Park it behind the completed text so
  // typing continues from the end.
  parkCaretAtEnd();
  hideCompletion();
  if (AbcUiElement *editor = node("console-input"))
    Api->ui->elementFocus(editor);
  // The Enter path of a Tab session runs the candidate it just completed.
  if (run)
    submitFromEditor();
}

// Shared dropdown builder. `apply` = the Tab path (unique match completes
// in place, multiple matches extend to the common prefix AND open the
// cycle session); the live path only presents.
void rebuildCompletion(const std::string &prefix, bool apply) {
  Suggestions.clear();
  SuggestionIndex = -1;
  // Any rebuild invalidates a session: only the Tab path below opens one
  // (a live refresh already means the editor text changed).
  CompletionSession = false;
  if (prefix.empty()) {
    hideCompletion();
    return;
  }
  // One extra match is requested so the dropdown can say that more exist
  // without knowing the catalog size.
  AbcUiConsoleSuggestion matches[kMaxSuggestions + 1];
  for (auto &match : matches)
    match.structSize = sizeof(match);
  const int total =
      Api->consolepage->complete(prefix.c_str(), matches, kMaxSuggestions + 1);
  const bool more = total > kMaxSuggestions;
  const int count = std::min<int>(total, kMaxSuggestions);
  for (int i = 0; i < count; ++i)
    Suggestions.push_back(matches[i].name);
  if (Suggestions.empty()) {
    hideCompletion();
    return;
  }
  AbcUiElement *editor = node("console-input");
  if (apply && Suggestions.size() == 1) {
    // Unique match: complete in place, no list. The caret must follow the
    // completed text (a value write alone leaves it behind the prefix).
    endEditorComposition();
    setInputValue("console-input", Suggestions[0]);
    parkCaretAtEnd();
    Suggestions.clear();
    hideCompletion();
    return;
  }
  // Multiple matches: extend the editor to the longest common prefix and
  // show the list.
  std::string common = Suggestions[0];
  for (const std::string &match : Suggestions) {
    std::size_t length = 0;
    while (length < common.size() && length < match.size() &&
           common[length] == match[length])
      ++length;
    common.resize(length);
  }
  if (apply && common.size() > prefix.size()) {
    endEditorComposition();
    setInputValue("console-input", common);
    parkCaretAtEnd();
  } else if (apply) {
    // The common prefix did not grow: the editor already holds it, but a
    // completion is still an edit boundary (the caret always ends behind
    // the text).
    parkCaretAtEnd();
  }
  AbcUiElement *list = node("console-suggestions");
  if (!list)
    return;
  for (int32_t i = Api->ui->elementChildCount(list) - 1; i >= 0; --i) {
    if (AbcUiElement *child = Api->ui->elementChild(list, i))
      Api->ui->elementRemove(child);
  }
  char indexText[16];
  for (int i = 0; i < count; ++i) {
    AbcUiElement *row = Api->ui->elementAppendRml(
        list, escapeText(Suggestions[static_cast<size_t>(i)]).c_str(),
        "console-suggestion");
    if (!row)
      continue;
    std::snprintf(indexText, sizeof(indexText), "%d", i);
    Api->ui->elementSetAttribute(row, "data-action", "console-suggestion");
    Api->ui->elementSetAttribute(row, "data-index", indexText);
  }
  if (more) {
    AbcUiElement *row =
        Api->ui->elementAppendRml(list, "&hellip;", "console-suggestion");
    if (row)
      Api->ui->elementSetClass(row, "console-suggestion-more", 1);
  }
  // The first Tab OPENS the cycle session - the list stays up with index 0
  // highlighted, and every later Tab advances it.
  if (apply) {
    CompletionSession = true;
    SuggestionIndex = 0;
    const int32_t rows = Api->ui->elementChildCount(list);
    for (int32_t i = 0; i < rows; ++i) {
      if (AbcUiElement *child = Api->ui->elementChild(list, i))
        Api->ui->elementSetClass(child, "selected", i == 0 ? 1 : 0);
    }
  }
  setDisplay("console-suggestions", true);
}

void openCompletionFor(const std::string &prefix) {
  rebuildCompletion(prefix, true);
}

// Live dropdown refresh while typing: recomputes the list for the current
// editor content without ever applying a completion (Tab/Enter do that).
void refreshSuggestions() {
  rebuildCompletion(inputValue("console-input"), false);
}

void submitFromEditor();

// Tab inside an open session advances the highlight and drops that
// candidate into the editor. The list keeps showing the SAME match set (a
// re-filter on the completed text would collapse it to one row).
void cycleCompletion() {
  if (Suggestions.empty())
    return;
  const int count = static_cast<int>(Suggestions.size());
  SuggestionIndex = SuggestionIndex < 0 ? 0 : (SuggestionIndex + 1) % count;
  endEditorComposition();
  setInputValue("console-input",
                Suggestions[static_cast<size_t>(SuggestionIndex)]);
  parkCaretAtEnd();
  AbcUiElement *list = node("console-suggestions");
  if (list) {
    const int32_t rows = Api->ui->elementChildCount(list);
    for (int32_t i = 0; i < rows; ++i) {
      if (AbcUiElement *child = Api->ui->elementChild(list, i))
        Api->ui->elementSetClass(child, "selected",
                                 i == SuggestionIndex ? 1 : 0);
    }
  }
  setDisplay("console-suggestions", true);
  if (AbcUiElement *editor = node("console-input"))
    Api->ui->elementFocus(editor);
}

// Blur (focus loss) ends a cycle session: Tab has no anchor without the
// caret. The list itself stays - a click on a row must still land.
void endCompletionSession() {
  CompletionSession = false;
}

// --- submission --------------------------------------------------------------

void openConfirm(const std::string &text);

// The vanilla console's input record: exactly one "] <command>" line in the
// same scrollback the output uses, written before the command is handed to
// the engine so it is present even when the command produces no output.
void recordSubmission(const std::string &command) {
  if (command.empty())
    return;
  std::string line = "] ";
  line += command;
  Api->consolepage->pushLocalLine(line.c_str());
}

void submitFromEditor() {
  const std::string text = inputValue("console-input");
  std::string trimmed = text;
  while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t'))
    trimmed.pop_back();
  if (trimmed.empty())
    return; // empty submissions are not typos worth clearing
  if (containsNewline(text)) {
    // Defense in depth (the widget strips newlines, the paste gate is
    // the real path): a newline-bearing submission never executes
    // directly either - it opens the confirm pane from the editor text.
    openConfirm(text);
    return;
  }
  setInputValue("console-input", "");
  HistoryCursor = -1;
  hideCompletion();
  recordSubmission(trimmed);
  int32_t unknown = 0;
  if (Api->consolepage->submit(trimmed.c_str(), &unknown) && unknown)
    Api->consolepage->noteUnknownCommand(trimmed.c_str());
}

void walkHistory(int delta) {
  const int32_t count = Api->consolepage->historyCount();
  if (count <= 0)
    return;
  char entry[ABCUI_CONSOLE_TEXT_MAX];
  if (HistoryCursor < 0 && delta > 0) {
    // Starting the walk: remember the live draft to restore on the way
    // back down.
    HistoryDraft = inputValue("console-input");
    HistoryCursor = 0;
  } else if (delta > 0) {
    if (HistoryCursor + 1 >= count)
      return; // oldest end
    ++HistoryCursor;
  } else {
    if (HistoryCursor <= 0) {
      // Back to the live draft.
      HistoryCursor = -1;
      setInputValue("console-input", HistoryDraft);
      parkCaretAtEnd();
      return;
    }
    --HistoryCursor;
  }
  if (!Api->consolepage->historyAt(count - 1 - HistoryCursor, entry,
                                   sizeof(entry)))
    return;
  setInputValue("console-input", entry);
  // Caret to the end so typing appends.
  parkCaretAtEnd();
}

// --- confirm pane
// -------------------------------------------------------------

void closeConfirm() {
  PendingText.clear();
  setDisplay("console-confirm", false);
}

void openConfirm(const std::string &text) {
  PendingText = text;
  const std::vector<std::string> lines = splitCommandLines(text);
  std::string lead = translate("Console_ConfirmLead");
  setInnerRml("console-confirm-lead",
              escapeText(lead + " " + std::to_string(lines.size())).c_str());
  // The full text, escaped, in a scrollable block.
  setInnerRml("console-confirm-text", escapeText(text));
  setDisplay("console-confirm", true);
  if (AbcUiElement *yes = node("console-confirm-yes"))
    Api->ui->elementFocus(yes);
}

void executeConfirmed() {
  if (PendingText.empty())
    return;
  const std::vector<std::string> lines = splitCommandLines(PendingText);
  closeConfirm();
  setInputValue("console-input", "");
  HistoryCursor = -1;
  for (const std::string &line : lines) {
    recordSubmission(line); // one input record per confirmed command line
    Api->consolepage->submit(line.c_str(),
                             nullptr); // one validated submission per line
  }
  if (AbcUiElement *editor = node("console-input"))
    Api->ui->elementFocus(editor);
}

// --- event handlers
// -------------------------------------------------------------

int32_t onClick(void *, AbcUiElement *target, const AbcUiElementEvent *event) {
  if (!event || !target || std::strcmp(event->type, "click"))
    return 0;
  AbcUiElement *entry = target;
  std::string action;
  while (entry) {
    action = attributeOf(entry, "data-action");
    if (!action.empty())
      break;
    entry = Api->ui->elementParent(entry);
  }
  if (!entry || action.empty())
    return 0;

  if (action == "console-close") {
    Api->consolepage->requestClose();
    return 1;
  }
  if (action == "console-context-copy") {
    // Right-click menu: the selection when there is one, otherwise the
    // whole visible scrollback. Ctrl+C remains the quick path.
    copyOutput();
    closeContextMenu();
    return 1;
  }
  if (action == "console-confirm-yes") {
    executeConfirmed();
    return 1;
  }
  if (action == "console-confirm-no") {
    // Cancel: nothing runs and the single-line editor never receives the
    // newline-bearing text (it could not hold it honestly). The paste is
    // dropped; the editor keeps whatever it had before the paste.
    closeConfirm();
    if (AbcUiElement *editor = node("console-input"))
      Api->ui->elementFocus(editor);
    return 1;
  }
  if (action == "console-suggestion") {
    // Fixed data-index attribute identifies the row (never the label).
    const int index = Api->ui->elementAttributeInt(entry, "data-index", -1);
    applyCompletion(index);
    return 1;
  }
  return 0;
}

int32_t onMousedown(void *, AbcUiElement *target,
                    const AbcUiElementEvent *event) {
  if (!event || !target || std::strcmp(event->type, "mousedown"))
    return 0;
  AbcUiElement *history = node("console-history");
  if (event->button == 1) {
    // Right click on output opens the copy menu and preserves the current
    // selection (the menu may be about to copy exactly that range).
    if (history && insideNode(target, history) &&
        (target == history || insideConsoleRow(target, history)))
      openContextMenu(event->mouseX, event->mouseY);
    else
      closeContextMenu();
    return 0;
  }
  if (event->button != 0)
    return 0;
  AbcUiElement *menu = node("console-context-menu");
  if (menu && insideNode(target, menu))
    return 0; // the item's click handler owns copy + close
  if (!history || !insideNode(target, history)) {
    closeContextMenu();
    clearSelection();
    return 0;
  }
  closeContextMenu();
  // The scrollbar owns its drag; only the output body and rows select.
  if (target != history && !insideConsoleRow(target, history))
    return 0;
  const Caret caret = caretFromPoint(event->mouseX, event->mouseY);
  if (caret.Row < 0)
    return 0;
  if ((event->modifiers & ABCUI_MOD_SHIFT) != 0 && !Selection.empty())
    Selection.extend(caret.Row, caret.Offset);
  else
    Selection.begin(caret.Row, caret.Offset);
  SelectionDirty = true;
  refreshSelectionOverlay(true);
  return 0;
}

int32_t onDrag(void *, AbcUiElement *target, const AbcUiElementEvent *event) {
  if (!event || std::strcmp(event->type, "drag"))
    return 0;
  if (!Selection.hasAnchor())
    return 0;
  AbcUiElement *history = node("console-history");
  if (!history || target != history)
    return 0;
  const Caret caret = caretFromPoint(event->mouseX, event->mouseY);
  if (caret.Row < 0)
    return 0;
  Selection.extend(caret.Row, caret.Offset);
  SelectionDirty = true;
  refreshSelectionOverlay(true);
  return 0;
}

int32_t onKeydown(void *, AbcUiElement *target,
                  const AbcUiElementEvent *event) {
  if (!event || std::strcmp(event->type, "keydown"))
    return 0;
  const int key = event->keynum;
  const bool ctrl = (event->modifiers & ABCUI_MOD_CTRL) != 0;
  const bool confirmOpen = !PendingText.empty();

  if (key == ABCUI_KEY_ESCAPE) {
    if (ContextMenuOpen) {
      // One key closes one layer: the context menu first. The menu owner's
      // routed ESC reads the claim, so this press cannot also close the
      // console.
      Api->consolepage->noteEscapeClaim();
      closeContextMenu();
      return 1;
    }
    if (confirmOpen) {
      // One key closes one layer: the pane first (the paste is dropped,
      // nothing runs).
      Api->consolepage->noteEscapeClaim();
      closeConfirm();
      if (AbcUiElement *editor = node("console-input"))
        Api->ui->elementFocus(editor);
      return 1;
    }
    if (!Suggestions.empty()) {
      // The completion list is a layer as well - one press closes only it.
      Api->consolepage->noteEscapeClaim();
      hideCompletion();
      if (AbcUiElement *editor = node("console-input"))
        Api->ui->elementFocus(editor);
      return 1;
    }
    return 0; // the overlay close is the owner's routed ESC, not ours
  }
  if (key == 'c' && ctrl && !Selection.empty()) {
    // Output selection copy. With no selection the widget keeps its own
    // Ctrl+C (editor text), so neither layer steals the other's shortcut.
    copySelection();
    return 1;
  }
  if (confirmOpen)
    return 0; // the modal pane drives; the editor stays inert

  if ((key == ABCUI_KEY_ENTER || key == ABCUI_KEY_KP_ENTER) && !ctrl) {
    // Enter inside a Tab session runs the HIGHLIGHTED candidate (the
    // shell menu-completion contract). A list that only the live typing
    // refresh opened keeps the plain submit of the typed text.
    if (CompletionSession && SuggestionIndex >= 0 &&
        SuggestionIndex < static_cast<int>(Suggestions.size())) {
      applyCompletion(SuggestionIndex, /*run=*/true);
      return 1;
    }
    submitFromEditor();
    return 1;
  }
  if (key == ABCUI_KEY_UPARROW || key == ABCUI_KEY_DOWNARROW) {
    const bool suggestionsShown = !Suggestions.empty();
    if (suggestionsShown) {
      // Walking the completion list while it is up.
      SuggestionIndex += key == ABCUI_KEY_DOWNARROW ? 1 : -1;
      if (SuggestionIndex < 0)
        SuggestionIndex = static_cast<int>(Suggestions.size()) - 1;
      if (SuggestionIndex >= static_cast<int>(Suggestions.size()))
        SuggestionIndex = 0;
      AbcUiElement *list = node("console-suggestions");
      if (list) {
        const int32_t rows = Api->ui->elementChildCount(list);
        for (int32_t i = 0; i < rows; ++i) {
          if (AbcUiElement *child = Api->ui->elementChild(list, i))
            Api->ui->elementSetClass(child, "selected",
                                     i == SuggestionIndex ? 1 : 0);
        }
      }
      return 1;
    }
    walkHistory(key == ABCUI_KEY_UPARROW ? 1 : -1);
    return 1;
  }
  if (key == 9) { // K_TAB
    // Never the document's focus navigation. An open session cycles; it
    // never re-filters. A fresh Tab opens the session on the typed prefix.
    if (CompletionSession && !Suggestions.empty()) {
      cycleCompletion();
      return 1;
    }
    openCompletionFor(inputValue("console-input"));
    return 1;
  }
  if (key == 'v' && ctrl) {
    // The multi-line paste gate: clipboard text bearing newlines is NEVER
    // handed to the editor (where it would merge into one line) and NEVER
    // executed silently. It opens the confirmation pane; only its explicit
    // Confirm submits one command per line.
    char clipboard[2048];
    clipboard[0] = '\0';
    const int needed = Api->shell->clipboardText(clipboard, sizeof(clipboard));
    if (needed > 0) {
      const std::string text(clipboard);
      if (containsNewline(text) && !splitCommandLines(text).empty())
        openConfirm(text);
    }
    return 0; // single-line paste is the widget's own Ctrl+V
  }
  return 0;
}

int32_t onKeyup(void *, AbcUiElement *target, const AbcUiElementEvent *event) {
  if (!event || std::strcmp(event->type, "keyup"))
    return 0;
  // Live suggestions: navigation keys keep the list they already own, the
  // modal pane owns the keyboard, and an empty editor hides it.
  const int released = event->keynum;
  const bool navigation =
      released == ABCUI_KEY_UPARROW || released == ABCUI_KEY_DOWNARROW ||
      released == ABCUI_KEY_ENTER || released == ABCUI_KEY_KP_ENTER ||
      released == 9 || released == ABCUI_KEY_ESCAPE;
  if (navigation || !PendingText.empty())
    return 0;
  // A Tab session's editor text is a completed CANDIDATE, not a fresh
  // prefix - re-filtering on it would collapse the list to that one entry.
  // Only a real edit (a non-navigation key) ends the session; the rebuild
  // below then lists the candidates of the edited text again.
  if (CompletionSession)
    endCompletionSession();
  refreshSuggestions();
  return 0;
}

int32_t onBlur(void *, AbcUiElement *target, const AbcUiElementEvent *event) {
  if (!event || std::strcmp(event->type, "blur"))
    return 0;
  endCompletionSession();
  return 0;
}

// --- presentation
// -------------------------------------------------------------

void refocusDefault() {
  if (AbcUiElement *editor = node("console-input"))
    Api->ui->elementFocus(editor);
}

void prepare() {
  setInputValue("console-input", "");
  PendingText.clear();
  Suggestions.clear();
  SuggestionIndex = -1;
  HistoryCursor = -1;
  HistoryDraft.clear();
  setDisplay("console-confirm", false);
  closeContextMenu();
  hideCompletion();
  Selection.clear();
  SelectionDirty = true;
  // A fresh open reseeds the projection: the tail window of whatever the
  // buffer holds now (the reader sees current state, not an arbitrary
  // incremental slice from a previous session).
  ProjectedSequence = 0;
  ProjectedGeneration = 0;
  render();
  refocusDefault();
}

void present() {
  if (!Page || !Api->consolepage)
    return;
  if (Api->documents->visible(Page))
    return;
  if (!Prepared) {
    Prepared = true;
    prepare();
  }
  Api->documents->show(Page);
  render();
}

void dismiss() {
  if (Page && Api->documents->visible(Page))
    Api->documents->hide(Page);
}

bool wantsConsole(const AbcUiMenuState *state) {
  return state && state->up && state->consoleOpen;
}

void onMenuState(void *, const void *payload) {
  const auto *state = static_cast<const AbcUiMenuState *>(payload);
  if (!state)
    return;
  if (wantsConsole(state))
    present();
  else
    dismiss();
}

void onMenuPaint(void *, const void *payload) {
  const auto *state = static_cast<const AbcUiMenuState *>(payload);
  if (!state || !wantsConsole(state) || !Page)
    return;
  if (!Api->documents->visible(Page)) {
    Prepared = false; // a fresh open rebuilds the editor state
    return;
  }
  // The engine output streams while the overlay is up: the page appends
  // the buffer's new lines once per frame (bounded incremental
  // projection; no-op when nothing new arrived).
  render();
}

void onDocumentReady(void *) {
  Prepared = false;
  BoundRoot = Api->documents->root(Page);
  if (!BoundRoot)
    return;
  ClickBinding =
      Api->ui->elementAddListener(BoundRoot, "click", &onClick, nullptr);
  // Output selection: mousedown anchors, drag extends. Capture phase so
  // the console's own widget/listener defaults cannot swallow the feed.
  MousedownBinding = Api->ui->elementAddListenerCapture(BoundRoot, "mousedown",
                                                        &onMousedown, nullptr);
  DragBinding =
      Api->ui->elementAddListenerCapture(BoundRoot, "drag", &onDrag, nullptr);
  // Capture phase: the interaction keys are consumed before the editor
  // widget (and the document's own TAB navigation default) sees them.
  KeydownBinding = Api->ui->elementAddListenerCapture(BoundRoot, "keydown",
                                                      &onKeydown, nullptr);
  // Live completion dropdown: the keyup that follows an edit re-reads the
  // editor and refreshes the list.
  KeyupBinding =
      Api->ui->elementAddListenerCapture(BoundRoot, "keyup", &onKeyup, nullptr);
  // A focus loss ends the Tab completion session (the caret is gone, so
  // cycling has no anchor). Attached to the editor itself: the blur event
  // does not bubble.
  if (AbcUiElement *editor = node("console-input"))
    BlurBinding = Api->ui->elementAddListener(editor, "blur", &onBlur, nullptr);
}

void onDocumentHide(void *) {
  Prepared = false;
}

void onDocumentClose(void *) {
  // The DOM is still alive: drop the listeners before it goes (the
  // framework owns the listener objects until they are removed).
  struct Binding {
    const char *type;
    uint64_t id;
  };
  const Binding bindings[] = {
      {"click", ClickBinding}, {"mousedown", MousedownBinding},
      {"drag", DragBinding},   {"keydown", KeydownBinding},
      {"keyup", KeyupBinding},
  };
  if (BoundRoot) {
    for (const Binding &binding : bindings) {
      if (binding.id)
        Api->ui->elementRemoveListener(BoundRoot, binding.type, binding.id);
    }
  }
  if (BlurBinding) {
    if (AbcUiElement *editor = node("console-input"))
      Api->ui->elementRemoveListener(editor, "blur", BlurBinding);
  }
  ClickBinding = MousedownBinding = DragBinding = 0;
  KeydownBinding = KeyupBinding = BlurBinding = 0;
  BoundRoot = nullptr;
  Prepared = false;
  RowTexts.clear();
  RowNodes.clear();
  Selection.clear();
}

} // namespace

void load() {
  StateSubscription = Api->events->subscribe(ABCUI_CHANNEL_MENU_STATE, 0,
                                             &onMenuState, nullptr);
  PaintSubscription = Api->events->subscribe(ABCUI_CHANNEL_MENU_PAINT, 0,
                                             &onMenuPaint, nullptr);

  AbcUiDocumentDesc desc{};
  desc.structSize = sizeof(desc);
  desc.name = kDocumentName;
  desc.path = kDocumentPath;
  desc.flags = ABCUI_DOC_FLAG_MENU;
  desc.zIndex = 0;
  desc.userdata = nullptr;
  desc.onReady = &onDocumentReady;
  desc.onShow = nullptr;
  desc.onHide = &onDocumentHide;
  desc.onClose = &onDocumentClose;
  Page = Api->documents->create(&desc);
}

void shutdown() {
  // The document dies with the context; forget every borrowed handle.
  Page = nullptr;
  BoundRoot = nullptr;
  ClickBinding = MousedownBinding = DragBinding = 0;
  KeydownBinding = KeyupBinding = BlurBinding = 0;
  Prepared = false;
  PendingText.clear();
  Suggestions.clear();
  SuggestionIndex = -1;
  HistoryCursor = -1;
  HistoryDraft.clear();
  RowTexts.clear();
  RowNodes.clear();
  Selection.clear();
  ContextMenuOpen = false;
}

void unload() {
  if (StateSubscription) {
    Api->events->unsubscribe(StateSubscription);
    StateSubscription = 0;
  }
  if (PaintSubscription) {
    Api->events->unsubscribe(PaintSubscription);
    PaintSubscription = 0;
  }
  Page = nullptr;
}

} // namespace consolepage
