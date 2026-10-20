#pragma once

// panels::console - the replacement console page (T16).
//
// The presentation half of the console replacement: a top-band overlay
// document (menuconsole) on the shared abcui context. It renders the bounded
// engine output mirror (services::console::ConsoleBuffer, T15) and drives
// the user input loop against the T15 command backend (history/completion
// are read-only reads; execution travels through the injected submit sink so
// the engine-call ownership stays with the module - the T09 architecture).
//
// Hard rules (T16):
//   - Output is ALWAYS text: every engine line is escaped before it reaches
//     the DOM; markup in engine output can never become structure.
//   - The live projection is bounded: the first render of a view takes only
//     the most recent window (kInitialLines), later renders append only new
//     lines, and the DOM holds at most kMaxDomLines rows (oldest trimmed).
//   - Scroll is honest: a reader parked above the bottom is never yanked
//     down by appends; a viewer at the bottom follows the tail.
//   - Output wraps to the console's actual laid-out width (no fixed columns,
//     no horizontal scrolling), and the same layout is the source for
//     mouse selection: drag over rows, Ctrl+C copies the selected text, and
//     a right-click opens the Copy menu (visible output when nothing is
//     selected).
//   - The editor is single-line: text enters only through the T07 text
//     pipeline, so an uncommitted composition can never execute. Enter
//     submits through the sink, Up/Down walk the backend history, Tab
//     completes through the backend catalog.
//   - A multi-line paste NEVER executes silently: Ctrl+V with newline-
//     bearing clipboard text opens the confirm pane; only its explicit
//     Confirm submits (one T15-validated command per line), Cancel restores
//     the text into the editor.
//   - Fixed intent names only (never user text): console-submit,
//     console-confirm-execute, console-confirm-cancel, console-close.

#include "../../framework/InteractiveDocument.h"
#include "ConsoleSelection.h"

#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Types.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Rml {
class Event;
class ElementDocument;
} // namespace Rml

namespace services::console {
class ConsoleBuffer;
class ConsoleBackend;
} // namespace services::console

namespace panels::console {

using ActionSink = std::function<void(const char *action)>;
// Command execution sink (the module wires this to the T15 backend submit
// path). One call per command line; a confirmed multi-line paste calls it
// once per line, in order.
// The submission seam returns the module's answer for the console's own
// unknown-command line: true = the client runtime catalog does not know the
// verb AND the answer belongs in this console (Sven Co-op's engine prints
// nothing for it, and a loaded level's server may still own the name - the
// module holds that fact). The page only formats and shows the line; engine
// call ownership stays with the module.
using SubmitSink = std::function<bool(const std::string &command)>;

class ConsolePage final : public framework::InteractiveDocument {
public:
  // Frozen projection caps (T16 record).
  static constexpr int kInitialLines = 200;  // first render of a view
  static constexpr int kMaxDomLines = 300;   // DOM row cap
  static constexpr int kMaxSuggestions = 10; // completion dropdown row cap

#ifndef NDEBUG
  // Debug seam (abc_ui_console_ui_type): drives the editor and the live
  // dropdown without an injectable keyboard.
  void debugType(const char *text);
#endif

  ConsolePage();
  ~ConsolePage() override;

  void setActionSink(ActionSink sink) {
    Sink = std::move(sink);
  }
  void setSubmitSink(SubmitSink sink) {
    Submit = std::move(sink);
  }
  // Non-const: the console's own local scrollback lines (the unknown-command
  // answer) ride the same bounded mirror the engine stream fills, exactly
  // where the vanilla console put its messages (see the two-producer note
  // in ConsoleBuffer.h).
  void setBuffer(services::console::ConsoleBuffer *buffer) {
    Buffer = buffer;
  }
  void setBackend(const services::console::ConsoleBackend *backend) {
    Backend = backend;
  }

  // Projects the buffer's new lines into the DOM (incremental, bounded).
  // Safe to call every frame and on every presentation.
  void render();

  // Fresh opening: clear the editor and confirm pane, reseed the
  // projection from the buffer's current tail, focus the editor.
  void prepare();

  // Re-takes the editor focus (module's per-frame focus guard).
  void refocusDefault();

  // T16 ESC layering: true exactly once after the confirm pane consumed
  // an ESC press through the Rml feed. The module's owner action runs
  // after that feed (router order: uiKeyDown then onClaimedKeyDown), so it
  // checks-and-clears this claim instead of closing a second layer in the
  // same physical press ("同一事件不关闭两层").
  bool consumeEscapeClaim() {
    const bool claimed = EscapeClaimed;
    EscapeClaimed = false;
    return claimed;
  }

protected:
  void onDocumentReady() override;

private:
  class WindowListener final : public Rml::EventListener {
  public:
    explicit WindowListener(ConsolePage *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    ConsolePage *Owner;
  };

  // Capture-phase keydown on the document: the interaction keys (Enter,
  // Up/Down, Tab, Ctrl+V paste inspection) are consumed before the editor
  // widget and the document's own TAB navigation see them.
  class KeyListener final : public Rml::EventListener {
  public:
    explicit KeyListener(ConsolePage *owner) : Owner(owner) {}
    void ProcessEvent(Rml::Event &event) override;

  private:
    ConsolePage *Owner;
  };

  void handleClick(Rml::Event &event);
  void handlePointerDown(Rml::Event &event);
  void handleDrag(Rml::Event &event);
  void handleKey(Rml::Event &event);
  Rml::ElementDocument *documentOrNull() const;

  // Projection.
  void appendLine(const std::string &text, bool developer, bool truncated);
  int trimDom();
  void updateCountLine();

  // Selection. `caretFromPoint` maps a context-space point onto the
  // laid-out output (row + UTF-8 byte offset); the highlight layer is
  // rebuilt from the same line boxes the text renderer used.
  struct Caret {
    int Row = -1;
    std::size_t Offset = 0;
  };
  Caret caretFromPoint(Rml::Vector2f point) const;
  void clearSelection();
  void copySelection();
  void copyOutput();
  void openContextMenu(Rml::Vector2f point);
  void closeContextMenu();
  void refreshSelectionOverlay(bool force = false);

  // Interaction.
  void submitFromEditor();
  void openCompletionFor(const std::string &prefix);
  // Live dropdown refresh while typing: recomputes the list for the current
  // editor content without ever applying a completion (Tab/Enter do that).
  void refreshSuggestions();
  // Shared dropdown builder. `apply` = the Tab path (unique match completes
  // in place, multiple matches extend to the common prefix AND open the
  // cycle session of issue #7); the live path only presents.
  void rebuildCompletion(const std::string &prefix, bool apply);
  void hideCompletion();
  // Tab cycling of an open completion session: advance the highlight, drop
  // that candidate into the editor, keep the list up. Never re-filters.
  void cycleCompletion();
  // Blur (focus loss) ends a cycle session: Tab has no anchor without the
  // caret. The list itself stays - a click on a row must still land.
  void endCompletionSession();
  // Applies a candidate to the editor and closes the list. `submit` runs it
  // (the Tab-session Enter path); a click only completes the text.
  void applyCompletion(int index, bool submit = false);
  void openConfirm(const std::string &text, bool fromPaste);
  void closeConfirm();
  void executeConfirmed();
  void answerUnknownCommand(const std::string &command);
  void walkHistory(int delta);

  WindowListener Listener;
  KeyListener Keys;
  ActionSink Sink;
  SubmitSink Submit;
  services::console::ConsoleBuffer *Buffer = nullptr;
  const services::console::ConsoleBackend *Backend = nullptr;

  // Projection cursor: the last projected buffer sequence and the buffer
  // generation the DOM view belongs to (a Clear bump reseeds the view).
  uint64_t ProjectedSequence = 0;
  // Tail follow across the live frame ordering: an append arms it, the next
  // render (fresh layout) applies it unless the reader scrolled up.
  bool FollowPending = false;
  float FollowFromScrollTop = 0.0f;
  uint64_t ProjectedGeneration = 0;
  // The pending multi-line text (confirm pane); empty = pane closed.
  std::string PendingText;
  bool PendingFromPaste = false;
  // Completion state for the current editor content.
  std::vector<std::string> Suggestions;
  int SuggestionIndex = -1;
  // Issue #7: an OPEN Tab completion session. The first Tab lists the
  // candidates (index 0 highlighted, editor extended to the common prefix);
  // every later Tab cycles index and drops that candidate into the editor.
  // A session is never re-filtered from its own completed text - only a real
  // edit, Esc, Enter/click or a blur ends it (see cycleCompletion).
  bool CompletionSession = false;
  // History walk cursor: -1 = live editing, else an offset from the tail
  // of the backend history; the draft restores on the way back down.
  int HistoryCursor = -1;
  std::string HistoryDraft;
  // Set when the confirm pane consumed an ESC press (see consumeEscapeClaim).
  bool EscapeClaimed = false;
  // The visible rows' source text, index-aligned with the history's
  // children (the DOM can be trimmed; this mirror trims with it).
  std::vector<std::string> RowTexts;
  ConsoleSelection Selection;
  bool SelectionDirty = false;
  bool ContextMenuOpen = false;
  // Layout inputs of the last highlight build: a scroll, resize, append or
  // trim invalidates the cached rectangles.
  float SelectionScrollTop = -1.0f;
  float SelectionScrollHeight = -1.0f;
  float SelectionWidth = -1.0f;
};

} // namespace panels::console
