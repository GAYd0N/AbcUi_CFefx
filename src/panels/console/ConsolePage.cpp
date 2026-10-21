// panels::console::ConsolePage implementation - see ConsolePage.h.
#include "ConsolePage.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/StringUtilities.h>

#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"
#include "../../rmlui/TextInput.h"
#include "../../services/console/ConsoleBackend.h"
#include "../../services/console/ConsoleBuffer.h"

#include <algorithm>
#include <cstdio>

namespace panels::console {

namespace {

constexpr const char *kActionSubmit = "console-submit";
constexpr const char *kActionConfirmExecute = "console-confirm-execute";
constexpr const char *kActionConfirmCancel = "console-confirm-cancel";
constexpr const char *kActionClose = "console-close";

Rml::Element *actionNode(Rml::Element *node) {
  for (Rml::Element *it = node; it; it = it->GetParentNode()) {
    if (it->HasAttribute("data-action"))
      return it;
  }
  return nullptr;
}

void setText(Rml::ElementDocument *doc, const char *id,
             const std::string &text) {
  if (!doc)
    return;
  if (Rml::Element *node = doc->GetElementById(id))
    node->SetInnerRML(framework::TextUtil::escapeRml(text));
}

void setVisible(Rml::ElementDocument *doc, const char *id, bool visible) {
  if (!doc)
    return;
  if (Rml::Element *node = doc->GetElementById(id))
    node->SetProperty("display", visible ? "block" : "none");
}

void setControlValue(Rml::ElementDocument *doc, const char *id,
                     const std::string &value) {
  if (!doc)
    return;
  if (auto *control =
          dynamic_cast<Rml::ElementFormControl *>(doc->GetElementById(id)))
    control->SetValue(value.c_str());
}

std::string controlValue(Rml::ElementDocument *doc, const char *id) {
  if (!doc)
    return {};
  if (auto *control =
          dynamic_cast<Rml::ElementFormControl *>(doc->GetElementById(id)))
    return control->GetValue().c_str();
  return {};
}

bool containsNewline(const std::string &text) {
  return text.find('\n') != std::string::npos ||
         text.find('\r') != std::string::npos;
}

bool insideNode(const Rml::Element *node, const Rml::Element *ancestor) {
  for (const Rml::Element *it = node; it; it = it->GetParentNode()) {
    if (it == ancestor)
      return true;
  }
  return false;
}

// A point belongs to the output only when it is inside a row's text; the
// history's own blank area also starts a selection, but its scrollbar (a
// child with drag=block) must keep owning the pointer.
bool insideConsoleRow(const Rml::Element *target, const Rml::Element *history) {
  for (const Rml::Element *it = target; it; it = it->GetParentNode()) {
    if (it->IsClassSet("console-line"))
      return true;
    if (it == history)
      return false;
  }
  return false;
}

// Width of a UTF-8 prefix in the laid-out text element. This is the same
// measurement path RmlUi used to format the line, so the caret can be placed
// between glyphs instead of estimating from character counts.
int prefixWidth(Rml::ElementText *text, const Rml::String &line,
                std::size_t bytes) {
  bytes = std::min(bytes, line.size());
  return Rml::ElementUtilities::GetStringWidth(
      text, Rml::StringView(line.data(), line.data() + bytes));
}

// Maps a horizontal offset inside one generated line to the nearest UTF-8
// character boundary. A click in the left/right half of a glyph snaps to the
// matching side, matching the feel of a native text selection.
std::size_t offsetForX(Rml::ElementText *text, const Rml::String &line,
                       float x) {
  if (x <= 0.0f || line.empty())
    return 0;
  std::size_t previous = 0;
  int previousWidth = 0;
  for (Rml::StringIteratorU8 it(line); it; ++it) {
    const std::ptrdiff_t offset = it.offset();
    if (offset <= 0 || offset > static_cast<std::ptrdiff_t>(line.size()))
      continue;
    const std::size_t current = static_cast<std::size_t>(offset);
    const int currentWidth = prefixWidth(text, line, current);
    if (static_cast<float>(currentWidth) >= x) {
      if (x - static_cast<float>(previousWidth) <=
          static_cast<float>(currentWidth) - x)
        return previous;
      return current;
    }
    previous = current;
    previousWidth = currentWidth;
  }
  return line.size();
}

// Parks the editor's caret at the end of its text. The widget's selection
// API takes CHARACTER offsets and has no "end" sentinel (SetSelectionRange
// with a negative offset resolves to the first character), so the real
// character count is the value that means "behind the text" - the state
// typing leaves and the state a history recall must restore.
void parkCaretAtEnd(Rml::Element *editor) {
  auto *input = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(editor);
  if (!input)
    return;
  const Rml::String value = input->GetValue();
  const int end = static_cast<int>(Rml::StringUtilities::LengthUTF8(
      Rml::StringView(value.data(), value.data() + value.size())));
  input->SetSelectionRange(end, end);
}

// Issue #6: a completion replaces the editor text wholesale, so a live IME
// composition must be dropped first - its commit would otherwise be
// appended to the completed candidate instead of replacing the prefix.
void endEditorComposition() {
  if (rmlui::textComposing())
    rmlui::cancelComposition();
}

// Splits a confirmed multi-line text into executable command lines: CR is
// dropped, empty/whitespace-only lines are skipped (the T15 backend refuses
// embedded newlines, so each line is its own validated submission).
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

} // namespace

ConsolePage::ConsolePage()
    : InteractiveDocument("menuconsole", "abc/abcui/gameui/rml/console.rml"),
      Listener(this), Keys(this) {}

ConsolePage::~ConsolePage() {
  close();
}

Rml::ElementDocument *ConsolePage::documentOrNull() const {
  return document();
}

void ConsolePage::onDocumentReady() {
  Rml::ElementDocument *doc = documentOrNull();
  if (doc) {
    addListener(doc, "click", &Listener);
    // Output selection: mousedown anchors, drag extends. Capture phase so
    // the console's own widget/listener defaults cannot swallow the feed.
    addListener(doc, "mousedown", &Listener, true);
    addListener(doc, "drag", &Listener, true);
    // Capture phase: the interaction keys are consumed before the editor
    // widget (and the document's own TAB navigation default) sees them.
    addListener(doc, "keydown", &Keys, true);
    // Live completion dropdown: the keyup that follows an edit re-reads
    // the editor and refreshes the list (the edit keydown is consumed
    // before the widget applies it, so the value is only current here).
    addListener(doc, "keyup", &Keys, true);
    // Issue #7: a focus loss ends the Tab completion session (the caret is
    // gone, so cycling has no anchor). Attached to the editor itself: the
    // blur event does not bubble.
    if (Rml::Element *editor = doc->GetElementById("console-input"))
      addListener(editor, "blur", &Keys);
  }
  // A document (re)build re-seeds the projection from the buffer; the
  // buffer itself survives document rebuilds (T15 process-scoped cache).
  ProjectedSequence = 0;
  ProjectedGeneration = 0;
  PendingText.clear();
  Suggestions.clear();
  SuggestionIndex = -1;
  HistoryCursor = -1;
  HistoryDraft.clear();
  Selection.clear();
  RowTexts.clear();
  SelectionDirty = true;
  prepare();
}

void ConsolePage::prepare() {
  Rml::ElementDocument *doc = documentOrNull();
  setControlValue(doc, "console-input", "");
  PendingText.clear();
  Suggestions.clear();
  SuggestionIndex = -1;
  HistoryCursor = -1;
  HistoryDraft.clear();
  setVisible(doc, "console-confirm", false);
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

void ConsolePage::refocusDefault() {
  if (Rml::Element *editor =
          documentOrNull() ? documentOrNull()->GetElementById("console-input")
                           : nullptr)
    editor->Focus();
}

// --- projection ------------------------------------------------------------

void ConsolePage::render() {
  Rml::ElementDocument *doc = documentOrNull();
  if (!doc || !Buffer)
    return;
  Rml::Element *history = doc->GetElementById("console-history");
  if (!history)
    return;

  const uint64_t generation = Buffer->generation();
  if (generation != ProjectedGeneration) {
    // Clear (or a rebuild): drop the whole view and start over from the
    // current tail window.
    ProjectedGeneration = generation;
    ProjectedSequence = 0;
    history->SetInnerRML("");
    RowTexts.clear();
    Selection.clear();
    SelectionDirty = true;
  }

  if (ProjectedSequence == 0) {
    // First render of this view: only the recent window (bounded), never
    // the full backlog - the cache is a tail by design.
    uint64_t since = 0;
    const uint64_t latest = Buffer->latestSequence();
    if (latest > static_cast<uint64_t>(kInitialLines))
      since = latest - static_cast<uint64_t>(kInitialLines);
    std::vector<services::console::ConsoleLine> lines;
    Buffer->copySince(since, lines, static_cast<std::size_t>(kInitialLines));
    const bool atBottom = true; // a fresh view starts at the tail
    for (const auto &line : lines)
      appendLine(line.Text, line.Developer, line.Truncated);
    ProjectedSequence = Buffer->latestSequence();
    trimDom();
    if (atBottom)
      history->SetScrollTop(history->GetScrollHeight());
    updateCountLine();
    refreshSelectionOverlay();
    return;
  }

  const uint64_t latest = Buffer->latestSequence();
  if (latest <= ProjectedSequence) {
    // The pending tail follow (see the append branch): the metrics are
    // current now, so the one-pass-behind scroll can land exactly at the
    // bottom - unless the reader scrolled up since it was armed.
    if (FollowPending && !Selection.hasAnchor()) {
      FollowPending = false;
      if (history->GetScrollTop() + 1.0f >= FollowFromScrollTop)
        history->SetScrollTop(history->GetScrollHeight());
    }
    updateCountLine();
    refreshSelectionOverlay();
    return; // no new lines
  }

  // Incremental append: only lines newer than the projected cursor.
  const float scrollTopBefore = history->GetScrollTop();
  const float scrollHeightBefore = history->GetScrollHeight();
  const float clientHeight = history->GetClientHeight();
  // A selection pins the view: appends during a drag (or while selected
  // text waits to be copied) must never yank the range off screen.
  const bool atBottom =
      !Selection.hasAnchor() &&
      scrollTopBefore + clientHeight >= scrollHeightBefore - 1.0f;

  std::vector<services::console::ConsoleLine> lines;
  // The consumer window is bounded too: a long gap (the page was hidden
  // while output burst) pages from the newest tail, never from an
  // unbounded backlog.
  Buffer->copySince(ProjectedSequence, lines,
                    static_cast<std::size_t>(kMaxDomLines));
  for (const auto &line : lines)
    appendLine(line.Text, line.Developer, line.Truncated);
  ProjectedSequence = Buffer->latestSequence();
  trimDom();
  if (atBottom) {
    // The scroll set below lands on the CURRENT layout only when the
    // context already laid the appended rows out. Live frames render
    // before Context::Update, so the bounds here are one layout pass
    // behind: the follow is re-armed and re-applied on the next render
    // (one frame later, imperceptible) against fresh metrics. Without
    // that second pass the pane stays exactly one append above the
    // bottom and then stops following entirely - the live tail froze.
    history->SetScrollTop(history->GetScrollHeight());
    FollowPending = true;
    FollowFromScrollTop = scrollTopBefore;
  } else {
    FollowPending = false;
    // The reader is parked above the bottom: keep the visible content
    // stable. Trimming the oldest rows shrinks the scroll height; the
    // scroll top compensates by the same amount.
    float delta = scrollHeightBefore - history->GetScrollHeight();
    float target = scrollTopBefore - delta;
    if (target < 0.0f)
      target = 0.0f;
    history->SetScrollTop(target);
  }
  updateCountLine();
  refreshSelectionOverlay();
}

void ConsolePage::appendLine(const std::string &text, bool developer,
                             bool truncated) {
  Rml::ElementDocument *doc = documentOrNull();
  Rml::Element *history =
      doc ? doc->GetElementById("console-history") : nullptr;
  if (!history)
    return;
  Rml::ElementPtr row = doc->CreateElement("div");
  if (!row)
    return;
  row->SetClass("console-line", true);
  if (developer)
    row->SetClass("console-dev", true);
  // Output is always text: escaped, never parsed as markup.
  row->SetInnerRML(
      framework::TextUtil::escapeRml(text + (truncated ? " …" : "")));
  history->AppendChild(std::move(row));
  RowTexts.push_back(text + (truncated ? " …" : ""));
  SelectionDirty = true;
}

int ConsolePage::trimDom() {
  Rml::ElementDocument *doc = documentOrNull();
  Rml::Element *history =
      doc ? doc->GetElementById("console-history") : nullptr;
  if (!history)
    return 0;
  int removed = 0;
  while (history->GetNumChildren() > kMaxDomLines) {
    Rml::Element *oldest = history->GetChild(0);
    if (!oldest)
      break;
    history->RemoveChild(oldest);
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
  return removed;
}

void ConsolePage::updateCountLine() {
  Rml::ElementDocument *doc = documentOrNull();
  if (!doc || !Buffer)
    return;
  // "total: N" - the buffer's own line count (the DOM holds a capped
  // window of it). Plain text; the token resolves through the lang table.
  std::string label = "#Console_Count";
  framework::Localization::translate(label, "Console_Count");
  setText(doc, "console-count",
          label + " " + std::to_string(Buffer->lineCount()));
}

// --- interaction -----------------------------------------------------------

void ConsolePage::handleClick(Rml::Event &event) {
  Rml::Element *entry = actionNode(event.GetTargetElement());
  if (!entry)
    return;
  const Rml::String action =
      entry->GetAttribute<Rml::String>("data-action", "");

  if (action == "console-close") {
    const ActionSink Callback = Sink;
    if (Callback)
      Callback(kActionClose);
    return;
  }
  if (action == "console-context-copy") {
    // Right-click menu: the selection when there is one, otherwise the
    // whole visible scrollback. Ctrl+C remains the quick path.
    copyOutput();
    closeContextMenu();
    return;
  }
  if (action == "console-confirm-yes") {
    executeConfirmed();
    return;
  }
  if (action == "console-confirm-no") {
    // Cancel: nothing runs and the single-line editor never receives the
    // newline-bearing text (it could not hold it honestly). The paste is
    // dropped; the editor keeps whatever it had before the paste.
    closeConfirm();
    refocusDefault();
    const ActionSink Callback = Sink;
    if (Callback)
      Callback(kActionConfirmCancel);
    return;
  }
  if (action == "console-suggestion") {
    // Fixed data-index attribute identifies the row (never the label).
    const int index = entry->GetAttribute<int>("data-index", -1);
    applyCompletion(index);
    return;
  }
}

void ConsolePage::handlePointerDown(Rml::Event &event) {
  Rml::ElementDocument *doc = documentOrNull();
  if (!doc)
    return;
  const int button = event.GetParameter<int>("button", -1);
  Rml::Element *history = doc->GetElementById("console-history");
  Rml::Element *target = event.GetTargetElement();
  const Rml::Vector2f point(event.GetParameter<float>("mouse_x", 0.0f),
                            event.GetParameter<float>("mouse_y", 0.0f));
  if (button == 1) {
    // Right click on output opens the copy menu and preserves the current
    // selection (the menu may be about to copy exactly that range).
    if (history && insideNode(target, history) &&
        (target == history || insideConsoleRow(target, history)))
      openContextMenu(point);
    else
      closeContextMenu();
    return;
  }
  if (button != 0)
    return;
  Rml::Element *menu = doc->GetElementById("console-context-menu");
  if (menu && insideNode(target, menu))
    return; // the item's click handler owns copy + close
  if (!history || !insideNode(target, history)) {
    closeContextMenu();
    clearSelection();
    return;
  }
  closeContextMenu();
  // The scrollbar owns its drag; only the output body and rows select.
  if (target != history && !insideConsoleRow(target, history))
    return;
  const Caret caret = caretFromPoint(point);
  if (caret.Row < 0)
    return;
  const bool shift = event.GetParameter<int>("shift_key", 0) != 0;
  if (shift && !Selection.empty())
    Selection.extend(caret.Row, caret.Offset);
  else
    Selection.begin(caret.Row, caret.Offset);
  SelectionDirty = true;
  refreshSelectionOverlay(true);
}

void ConsolePage::handleDrag(Rml::Event &event) {
  if (!Selection.hasAnchor())
    return;
  Rml::ElementDocument *doc = documentOrNull();
  if (!doc)
    return;
  Rml::Element *history = doc->GetElementById("console-history");
  const Rml::Element *dragElement = static_cast<const Rml::Element *>(
      event.GetParameter<void *>("drag_element", static_cast<void *>(nullptr)));
  if (!history || dragElement != history)
    return;
  const Rml::Vector2f point(event.GetParameter<float>("mouse_x", 0.0f),
                            event.GetParameter<float>("mouse_y", 0.0f));
  const Caret caret = caretFromPoint(point);
  if (caret.Row < 0)
    return;
  Selection.extend(caret.Row, caret.Offset);
  SelectionDirty = true;
  refreshSelectionOverlay(true);
}

ConsolePage::Caret ConsolePage::caretFromPoint(Rml::Vector2f point) const {
  Rml::ElementDocument *doc = documentOrNull();
  Rml::Element *history =
      doc ? doc->GetElementById("console-history") : nullptr;
  if (!history || RowTexts.empty())
    return {};
  const int rowCount =
      std::min(history->GetNumChildren(), static_cast<int>(RowTexts.size()));
  if (rowCount <= 0)
    return {};

  int row = rowCount - 1;
  for (int i = 0; i < rowCount; ++i) {
    Rml::Element *candidate = history->GetChild(i);
    if (!candidate)
      continue;
    const float top = candidate->GetAbsoluteOffset().y;
    if (point.y < top + candidate->GetOffsetHeight()) {
      row = i;
      break;
    }
  }

  Rml::Element *rowElement = history->GetChild(row);
  Rml::ElementText *text =
      rowElement
          ? rmlui_dynamic_cast<Rml::ElementText *>(rowElement->GetFirstChild())
          : nullptr;
  if (!text)
    return Caret{row, 0};
  const auto &lines = text->GetLines();
  if (lines.empty())
    return Caret{row, 0};
  const Rml::Vector2f textOrigin = text->GetAbsoluteOffset();
  const float relativeY = point.y - textOrigin.y;
  int lineIndex = 0;
  for (int i = 1; i < static_cast<int>(lines.size()); ++i) {
    if (relativeY < lines[static_cast<std::size_t>(i)].position.y)
      break;
    lineIndex = i;
  }
  const Rml::ElementText::Line &line =
      lines[static_cast<std::size_t>(lineIndex)];
  std::size_t lineStart = 0;
  for (int i = 0; i < lineIndex; ++i)
    lineStart += lines[static_cast<std::size_t>(i)].text.size();
  const float relativeX = point.x - textOrigin.x - line.position.x;
  const std::size_t local = offsetForX(text, line.text, relativeX);
  const std::size_t offset = std::min(
      lineStart + local, RowTexts[static_cast<std::size_t>(row)].size());
  return Caret{row, offset};
}

void ConsolePage::clearSelection() {
  if (!Selection.hasAnchor())
    return;
  Selection.clear();
  SelectionDirty = true;
  refreshSelectionOverlay(true);
}

void ConsolePage::copySelection() {
  if (Selection.empty())
    return;
  Rml::SystemInterface *systemInterface = Rml::GetSystemInterface();
  if (!systemInterface)
    return;
  const std::string text = Selection.text(RowTexts);
  if (!text.empty())
    systemInterface->SetClipboardText(text);
}

void ConsolePage::copyOutput() {
  Rml::SystemInterface *systemInterface = Rml::GetSystemInterface();
  if (!systemInterface)
    return;
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
    systemInterface->SetClipboardText(text);
}

void ConsolePage::openContextMenu(Rml::Vector2f point) {
  Rml::ElementDocument *doc = documentOrNull();
  Rml::Element *menu =
      doc ? doc->GetElementById("console-context-menu") : nullptr;
  if (!doc || !menu)
    return;
  // The menu uses viewport pixel coordinates (the same space as the mouse
  // event); clamp it so a right click near an edge still shows the whole
  // entry. Its size is known from console.rcss (86dp min width, one 20dp row).
  const float scale =
      Rml::ElementUtilities::GetDensityIndependentPixelRatio(doc);
  const float menuWidth = 94.0f * scale;
  const float menuHeight = 31.0f * scale;
  const float x = std::clamp(point.x, 0.0f,
                             std::max(0.0f, doc->GetClientWidth() - menuWidth));
  const float y = std::clamp(
      point.y, 0.0f, std::max(0.0f, doc->GetClientHeight() - menuHeight));
  menu->SetProperty(Rml::PropertyId::Left, Rml::Property(x, Rml::Unit::PX));
  menu->SetProperty(Rml::PropertyId::Top, Rml::Property(y, Rml::Unit::PX));
  setVisible(doc, "console-context-menu", true);
  ContextMenuOpen = true;
}

void ConsolePage::closeContextMenu() {
  ContextMenuOpen = false;
  setVisible(documentOrNull(), "console-context-menu", false);
}

void ConsolePage::refreshSelectionOverlay(bool force) {
  Rml::ElementDocument *doc = documentOrNull();
  Rml::Element *history =
      doc ? doc->GetElementById("console-history") : nullptr;
  Rml::Element *layer =
      doc ? doc->GetElementById("console-selection") : nullptr;
  if (!history || !layer)
    return;
  if (Selection.empty()) {
    if (layer->GetNumChildren() != 0)
      layer->SetInnerRML("");
    SelectionDirty = false;
    return;
  }
  const float scrollTop = history->GetScrollTop();
  const float scrollHeight = history->GetScrollHeight();
  const float width = history->GetOffsetWidth();
  if (!force && !SelectionDirty && scrollTop == SelectionScrollTop &&
      scrollHeight == SelectionScrollHeight && width == SelectionWidth)
    return;
  SelectionDirty = false;
  SelectionScrollTop = scrollTop;
  SelectionScrollHeight = scrollHeight;
  SelectionWidth = width;
  layer->SetInnerRML("");

  const ConsoleSelection::Point first = Selection.start();
  const ConsoleSelection::Point last = Selection.end();
  const int rowCount =
      std::min(history->GetNumChildren(), static_cast<int>(RowTexts.size()));
  const Rml::Vector2f layerOrigin = layer->GetAbsoluteOffset();
  const float layerHeight = layer->GetOffsetHeight();
  for (int row = first.Row; row <= last.Row && row < rowCount; ++row) {
    if (row < 0)
      continue;
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!Selection.rowRange(row, RowTexts[static_cast<std::size_t>(row)].size(),
                            begin, end))
      continue;
    Rml::Element *rowElement = history->GetChild(row);
    Rml::ElementText *text = rowElement
                                 ? rmlui_dynamic_cast<Rml::ElementText *>(
                                       rowElement->GetFirstChild())
                                 : nullptr;
    if (!text)
      continue;
    const auto &lines = text->GetLines();
    const Rml::Vector2f textOrigin = text->GetAbsoluteOffset();
    const float lineHeight = text->GetLineHeight();
    std::size_t lineStart = 0;
    for (const Rml::ElementText::Line &line : lines) {
      const std::size_t currentStart = lineStart;
      const std::size_t currentEnd = currentStart + line.text.size();
      const std::size_t selectedBegin = std::max(begin, currentStart);
      const std::size_t selectedEnd = std::min(end, currentEnd);
      lineStart = currentEnd;
      if (selectedBegin >= selectedEnd)
        continue;
      const float x = textOrigin.x + line.position.x +
                      static_cast<float>(prefixWidth(
                          text, line.text, selectedBegin - currentStart)) -
                      layerOrigin.x;
      const float right = textOrigin.x + line.position.x +
                          static_cast<float>(prefixWidth(
                              text, line.text, selectedEnd - currentStart)) -
                          layerOrigin.x;
      const float y = textOrigin.y + line.position.y - layerOrigin.y;
      if (right <= x || y + lineHeight <= 0.0f || y >= layerHeight)
        continue;
      Rml::ElementPtr rectangle = doc->CreateElement("div");
      if (!rectangle)
        continue;
      rectangle->SetClass("console-selection-rect", true);
      char style[192];
      std::snprintf(style, sizeof(style),
                    "left:%.2fpx; top:%.2fpx; width:%.2fpx; height:%.2fpx;", x,
                    y, right - x, lineHeight);
      rectangle->SetAttribute("style", style);
      layer->AppendChild(std::move(rectangle));
    }
  }
}

void ConsolePage::handleKey(Rml::Event &event) {
  Rml::ElementDocument *doc = documentOrNull();
  if (!doc)
    return;
  if (event.GetId() == Rml::EventId::Keyup) {
    // Live suggestions: navigation keys keep the list they already own,
    // the modal pane owns the keyboard, and an empty editor hides it.
    const int released = event.GetParameter<int>("key_identifier", 0);
    const bool navigation =
        released == Rml::Input::KI_UP || released == Rml::Input::KI_DOWN ||
        released == Rml::Input::KI_RETURN ||
        released == Rml::Input::KI_NUMPADENTER ||
        released == Rml::Input::KI_TAB || released == Rml::Input::KI_ESCAPE;
    if (navigation || !PendingText.empty())
      return;
    // Issue #7: a Tab session's editor text is a completed CANDIDATE, not a
    // fresh prefix - re-filtering on it would collapse the list to that one
    // entry. Only a real edit (a non-navigation key) ends the session; the
    // rebuild below then lists the candidates of the edited text again.
    if (CompletionSession)
      endCompletionSession();
    refreshSuggestions();
    return;
  }
  const int key = event.GetParameter<int>("key_identifier", 0);
  const bool ctrl = event.GetParameter<int>("ctrl_key", 0) != 0;
  const bool confirmOpen = !PendingText.empty();

  if (key == Rml::Input::KI_ESCAPE) {
    if (ContextMenuOpen) {
      // One key closes one layer: the context menu first. The module's
      // owner action runs after this feed, so the claim keeps the same
      // press from also closing the console.
      event.StopPropagation();
      EscapeClaimed = true;
      closeContextMenu();
      return;
    }
    if (confirmOpen) {
      // One key closes one layer: the pane first (the paste is
      // dropped, nothing runs). The module's owner action runs after
      // this feed; the claim stops it from closing the console in the
      // same physical press.
      event.StopPropagation();
      EscapeClaimed = true;
      closeConfirm();
      refocusDefault();
      return;
    }
    if (!Suggestions.empty()) {
      // Issue #7: the completion list is a layer as well - one press closes
      // only it, and the claim keeps that press from also closing the
      // console (the same rule as the two panes above).
      event.StopPropagation();
      EscapeClaimed = true;
      hideCompletion();
      refocusDefault();
      return;
    }
    return; // the overlay close is the owner's routed ESC, not ours
  }
  if (key == Rml::Input::KI_C && ctrl && !Selection.empty()) {
    // Output selection copy. With no selection the widget keeps its own
    // Ctrl+C (editor text), so neither layer steals the other's shortcut.
    event.StopPropagation();
    copySelection();
    return;
  }
  if (confirmOpen)
    return; // the modal pane drives; the editor stays inert

  if ((key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER) &&
      !ctrl) {
    event.StopPropagation();
    // Issue #7: Enter inside a Tab session runs the HIGHLIGHTED candidate
    // (the shell menu-completion contract). A list that only the live
    // typing refresh opened keeps the plain submit of the typed text - the
    // pre-existing Enter behaviour.
    if (CompletionSession && SuggestionIndex >= 0 &&
        SuggestionIndex < static_cast<int>(Suggestions.size())) {
      applyCompletion(SuggestionIndex, /*submit=*/true);
      return;
    }
    submitFromEditor();
    return;
  }
  if (key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN) {
    const bool suggestionsShown = !Suggestions.empty();
    if (suggestionsShown) {
      // Walking the completion list while it is up.
      event.StopPropagation();
      SuggestionIndex += key == Rml::Input::KI_DOWN ? 1 : -1;
      if (SuggestionIndex < 0)
        SuggestionIndex = static_cast<int>(Suggestions.size()) - 1;
      if (SuggestionIndex >= static_cast<int>(Suggestions.size()))
        SuggestionIndex = 0;
      Rml::Element *list = doc->GetElementById("console-suggestions");
      if (list) {
        for (int i = 0; i < list->GetNumChildren(); ++i)
          list->GetChild(i)->SetClass("selected", i == SuggestionIndex);
      }
      return;
    }
    event.StopPropagation();
    walkHistory(key == Rml::Input::KI_UP ? 1 : -1);
    return;
  }
  if (key == Rml::Input::KI_TAB) {
    event.StopPropagation(); // never the document's focus navigation
    // Issue #7: an open session cycles; it never re-filters (see the keyup
    // rule above). A fresh Tab opens the session on the typed prefix.
    if (CompletionSession && !Suggestions.empty()) {
      cycleCompletion();
      return;
    }
    openCompletionFor(controlValue(doc, "console-input"));
    return;
  }
  if (key == Rml::Input::KI_V && ctrl) {
    // The multi-line paste gate: clipboard text bearing newlines is
    // NEVER handed to the editor (where it would merge into one line)
    // and NEVER executed silently. It opens the confirmation pane; only
    // its explicit Confirm submits one command per line.
    Rml::String clipboard;
    if (Rml::SystemInterface *systemInterface = Rml::GetSystemInterface())
      systemInterface->GetClipboardText(clipboard);
    const std::string text = clipboard.c_str();
    if (containsNewline(text) && !splitCommandLines(text).empty()) {
      event.StopPropagation();
      openConfirm(text, true);
    }
    return; // single-line paste is the widget's own Ctrl+V
  }
}

void ConsolePage::submitFromEditor() {
  Rml::ElementDocument *doc = documentOrNull();
  if (!doc)
    return;
  const std::string text = controlValue(doc, "console-input");
  std::string trimmed = text;
  while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t'))
    trimmed.pop_back();
  if (trimmed.empty())
    return; // empty submissions are not typos worth clearing
  if (containsNewline(text)) {
    // Defense in depth (the widget strips newlines, the paste gate is
    // the real path): a newline-bearing submission never executes
    // directly either.
    openConfirm(text, false);
    return;
  }
  setControlValue(doc, "console-input", "");
  HistoryCursor = -1;
  hideCompletion();
  recordSubmission(trimmed);
  if (Submit && Submit(trimmed))
    answerUnknownCommand(trimmed);
  const ActionSink Callback = Sink;
  if (Callback)
    Callback(kActionSubmit);
}

// The vanilla console's input record: exactly one "] <command>" line in the
// same scrollback the output uses, written before the command is handed to
// the engine so it is present even when the backend refuses or the command
// produces no output. The text is plain input (the buffer never parses
// markup); recordSubmission is deliberately separate from
// answerUnknownCommand, which represents engine-side output.
void ConsolePage::recordSubmission(const std::string &command) {
  if (!Buffer || command.empty())
    return;
  std::string line = "] ";
  line += command;
  line += '\n'; // one complete console line
  Buffer->pushText(line.data(), line.size(), false);
}

// The console's own answer for a command the client runtime catalog does not
// know (the module already decided it belongs here: the engine is silent for
// those, and a loaded level's server may own the name). The line rides the
// same scrollback the player reads - exactly where the vanilla console put
// its messages - and goes through the same escaping the projection uses.
void ConsolePage::answerUnknownCommand(const std::string &command) {
  if (!Buffer)
    return;
  const std::string verb =
      services::console::ConsoleBackend::verbOf(command.c_str());
  if (verb.empty())
    return; // not classifiable: never guess a name
  std::string line;
  if (!framework::Localization::translate(line, "Console_UnknownCommand"))
    line = "Unknown command: %s";
  const std::string marker = "%s";
  const auto at = line.find(marker);
  if (at != std::string::npos)
    line.replace(at, marker.size(), verb);
  line += '\n'; // one complete console line
  Buffer->pushText(line.data(), line.size(), false);
}

void ConsolePage::openCompletionFor(const std::string &prefix) {
  rebuildCompletion(prefix, true);
}

void ConsolePage::refreshSuggestions() {
  Rml::ElementDocument *doc = documentOrNull();
  if (!doc)
    return;
  rebuildCompletion(controlValue(doc, "console-input"), false);
}

void ConsolePage::rebuildCompletion(const std::string &prefix, bool apply) {
  Rml::ElementDocument *doc = documentOrNull();
  Suggestions.clear();
  SuggestionIndex = -1;
  // Any rebuild invalidates a session: only the Tab path below opens one
  // (a live refresh already means the editor text changed).
  CompletionSession = false;
  if (!doc || !Backend || prefix.empty()) {
    hideCompletion();
    return;
  }
  // One extra match is requested so the dropdown can say that more exist
  // without knowing the catalog size (the enumeration itself is capped in
  // the backend, which is what keeps a 3000-entry catalog from stalling).
  std::vector<std::string> matches =
      Backend->complete(prefix.c_str(), kMaxSuggestions + 1);
  const bool more = static_cast<int>(matches.size()) > kMaxSuggestions;
  if (more)
    matches.resize(kMaxSuggestions);
  Suggestions = std::move(matches);
  if (Suggestions.empty()) {
    hideCompletion();
    return;
  }
  if (apply && Suggestions.size() == 1) {
    // Unique match: complete in place, no list. Issue #6: the caret must
    // follow the completed text (SetValue writes the attribute only, so
    // typing would otherwise resume behind the typed prefix).
    endEditorComposition();
    setControlValue(doc, "console-input", Suggestions[0]);
    parkCaretAtEnd(doc->GetElementById("console-input"));
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
    setControlValue(doc, "console-input", common);
    parkCaretAtEnd(doc->GetElementById("console-input"));
  } else if (apply) {
    // The common prefix did not grow: the editor already holds it, but a
    // completion is still an edit boundary (issue #6 - completion always
    // leaves the caret behind the text).
    parkCaretAtEnd(doc->GetElementById("console-input"));
  }
  Rml::Element *list = doc->GetElementById("console-suggestions");
  if (!list)
    return;
  list->SetInnerRML("");
  for (std::size_t i = 0; i < Suggestions.size(); ++i) {
    Rml::ElementPtr row = doc->CreateElement("div");
    if (!row)
      continue;
    row->SetAttribute("data-action", "console-suggestion");
    row->SetAttribute("data-index", static_cast<int>(i));
    row->SetClass("console-suggestion", true);
    row->SetInnerRML(framework::TextUtil::escapeRml(Suggestions[i]));
    list->AppendChild(std::move(row));
  }
  if (more) {
    Rml::ElementPtr row = doc->CreateElement("div");
    if (row) {
      row->SetClass("console-suggestion", true);
      row->SetClass("console-suggestion-more", true);
      row->SetInnerRML("&hellip;");
      list->AppendChild(std::move(row));
    }
  }
  // Issue #7: the first Tab OPENS the cycle session - the list stays up with
  // index 0 highlighted, and every later Tab advances it.
  if (apply) {
    CompletionSession = true;
    SuggestionIndex = 0;
    for (int i = 0; i < list->GetNumChildren(); ++i)
      list->GetChild(i)->SetClass("selected", i == 0);
  }
  setVisible(doc, "console-suggestions", true);
}

void ConsolePage::hideCompletion() {
  Rml::ElementDocument *doc = documentOrNull();
  Suggestions.clear();
  SuggestionIndex = -1;
  CompletionSession = false;
  setVisible(doc, "console-suggestions", false);
}

void ConsolePage::endCompletionSession() {
  // Ends the Tab CYCLE only: the candidate list stays rendered so a click
  // can still apply a row (the session invariant is "Tab advances it").
  CompletionSession = false;
}

// Issue #7: Tab inside an open session advances the highlight and drops that
// candidate into the editor. The list keeps showing the SAME match set (a
// re-filter on the completed text would collapse it to one row) and the
// caret follows so typing continues from the end.
void ConsolePage::cycleCompletion() {
  Rml::ElementDocument *doc = documentOrNull();
  if (!doc || Suggestions.empty())
    return;
  const int count = static_cast<int>(Suggestions.size());
  SuggestionIndex = SuggestionIndex < 0 ? 0 : (SuggestionIndex + 1) % count;
  endEditorComposition();
  setControlValue(doc, "console-input", Suggestions[SuggestionIndex]);
  parkCaretAtEnd(doc->GetElementById("console-input"));
  Rml::Element *list = doc->GetElementById("console-suggestions");
  if (list) {
    for (int i = 0; i < list->GetNumChildren(); ++i)
      list->GetChild(i)->SetClass("selected", i == SuggestionIndex);
  }
  setVisible(doc, "console-suggestions", true);
  refocusDefault();
}

#ifndef NDEBUG
void ConsolePage::debugType(const char *text) {
  Rml::ElementDocument *doc = documentOrNull();
  setControlValue(doc, "console-input", text ? text : "");
  // The debug seam stands in for real typing, and typing leaves the caret
  // behind the inserted text: park it at the end so caret-moving keys
  // (LEFT/RIGHT) have the same starting point they would after real input.
  if (doc)
    parkCaretAtEnd(doc->GetElementById("console-input"));
  refreshSuggestions();
}
#endif

void ConsolePage::applyCompletion(int index, bool submit) {
  Rml::ElementDocument *doc = documentOrNull();
  if (!doc || index < 0 || index >= static_cast<int>(Suggestions.size()))
    return;
  endEditorComposition();
  setControlValue(doc, "console-input", Suggestions[index]);
  // Issue #6: SetValue() only writes the value attribute; the widget's
  // caret stays where the typed prefix left it. Park it behind the
  // completed text so typing continues from the end (the same rule the
  // history recall follows).
  parkCaretAtEnd(doc->GetElementById("console-input"));
  hideCompletion();
  refocusDefault();
  // The Enter path of a Tab session runs the candidate it just completed.
  if (submit)
    submitFromEditor();
}

void ConsolePage::openConfirm(const std::string &text, bool fromPaste) {
  Rml::ElementDocument *doc = documentOrNull();
  if (!doc)
    return;
  PendingText = text;
  PendingFromPaste = fromPaste;
  const std::vector<std::string> lines = splitCommandLines(text);
  std::string lead = "#Console_ConfirmLead";
  framework::Localization::translate(lead, "Console_ConfirmLead");
  setText(doc, "console-confirm-lead",
          lead + " " + std::to_string(lines.size()));
  // The full text, escaped, in a scrollable block.
  setText(doc, "console-confirm-text", text);
  setVisible(doc, "console-confirm", true);
  if (Rml::Element *yes = doc->GetElementById("console-confirm-yes"))
    yes->Focus();
}

void ConsolePage::closeConfirm() {
  PendingText.clear();
  PendingFromPaste = false;
  setVisible(documentOrNull(), "console-confirm", false);
}

void ConsolePage::executeConfirmed() {
  Rml::ElementDocument *doc = documentOrNull();
  if (PendingText.empty())
    return;
  const std::vector<std::string> lines = splitCommandLines(PendingText);
  closeConfirm();
  if (doc)
    setControlValue(doc, "console-input", "");
  HistoryCursor = -1;
  for (const std::string &line : lines) {
    recordSubmission(line); // one input record per confirmed command line
    if (Submit)
      Submit(line); // one validated submission per line, in order
  }
  refocusDefault();
  const ActionSink Callback = Sink;
  if (Callback)
    Callback(kActionConfirmExecute);
}

void ConsolePage::walkHistory(int delta) {
  Rml::ElementDocument *doc = documentOrNull();
  if (!doc || !Backend)
    return;
  const auto &history = Backend->history();
  if (history.empty())
    return;
  if (HistoryCursor < 0 && delta > 0) {
    // Starting the walk: remember the live draft to restore on the way
    // back down.
    HistoryDraft = controlValue(doc, "console-input");
    HistoryCursor = 0;
  } else if (delta > 0) {
    if (HistoryCursor + 1 >= static_cast<int>(history.size()))
      return; // oldest end
    ++HistoryCursor;
  } else {
    if (HistoryCursor <= 0) {
      // Back to the live draft.
      HistoryCursor = -1;
      setControlValue(doc, "console-input", HistoryDraft);
      parkCaretAtEnd(doc->GetElementById("console-input"));
      return;
    }
    --HistoryCursor;
  }
  const int index = static_cast<int>(history.size()) - 1 - HistoryCursor;
  setControlValue(doc, "console-input",
                  history[static_cast<std::size_t>(index)]);
  // Caret to the end so typing appends (the widget's selection rides the
  // parent element's selection API, not the form-control base).
  parkCaretAtEnd(doc->GetElementById("console-input"));
}

void ConsolePage::WindowListener::ProcessEvent(Rml::Event &event) {
  if (event.GetId() == Rml::EventId::Mousedown)
    Owner->handlePointerDown(event);
  else if (event.GetId() == Rml::EventId::Drag)
    Owner->handleDrag(event);
  else
    Owner->handleClick(event);
}

void ConsolePage::KeyListener::ProcessEvent(Rml::Event &event) {
  if (event.GetId() == Rml::EventId::Blur) {
    Owner->endCompletionSession();
    return;
  }
  Owner->handleKey(event);
}

} // namespace panels::console
