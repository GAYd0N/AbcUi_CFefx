// rmlui::TextInput implementation - see TextInput.h.
#include "TextInput.h"

#include "ImeOverlay.h"
#include "RmlUiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/TextInputContext.h>
#include <RmlUi/Core/Traits.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>

#include <keydefs.h>

#include "../framework/TextEncoding.h"

#include <Windows.h>

#include <algorithm>
#include <string_view>

namespace rmlui {

namespace {

// The SDL2-based engine's text event identities (SDL3-style ids; hw.dll's
// SDL event loop dispatches on the same values - execution/T07.md).
constexpr unsigned kSdlEventTextEditing = 0x302;
constexpr unsigned kSdlEventTextInput = 0x303;
constexpr unsigned kSdlEventTextEditingCandidates = 0x304;

// Event payload views over the SDL_Event union the BaseUI AppProc seam
// forwards (void* pevent). Layout evidence: the working VGUI2Extension IME
// handler declares exactly these shapes against this game install
// (thirdparty/VGUI2Extension/src/exportfuncs.cpp:40-76) - SDL3-style ids,
// SDL2-style inline 32-byte text buffers (hw.dll reads the committed string
// at offset 12 of the same union). No SDL headers are pulled in; these are
// read-only views of an opaque pointer, never an ABI upgrade of a
// dependency.
// Rml's StringView predates std::string_view implicit conversion; this is
// the one bridging helper.
Rml::StringView rmlView(std::string_view view) {
  return Rml::StringView(view.data(), view.data() + view.size());
}

struct SdlTextInputEvent {
  unsigned Type;
  unsigned Timestamp;
  unsigned WindowId;
  char Text[32];
};

struct SdlTextEditingEvent {
  unsigned Type;
  unsigned Timestamp;
  unsigned WindowId;
  char Text[32];
  int Start;
  int Length;
};

// The candidates event's payload shape has flipped between engine builds
// (the documented one crashed live), so no struct view is declared for it:
// the handler probes the raw words with full pointer validation and every
// event is dumped raw for the on-machine probe (see the 0x304 branch).

HWND gameWindow();

// Composition state machine over Rml's TextInputContext, following the pin
// backend reference (Backends/RmlUi_Platform_Win32.cpp
// TextInputMethodEditor_Win32) re-keyed from WM_IME_* messages to the SDL
// text events. Scoped: tracking only runs while one of OUR editors holds
// focus (OnActivate/OnDeactivate/OnDestroy come from the pin's
// WidgetTextInput), so composition that belongs to the original GameUI's
// TextEntry widgets never touches this state.
class TextInputAdapter final : public Rml::TextInputHandler {
public:
  void OnActivate(Rml::TextInputContext *input_context) override {
    InputContext = input_context;
  }
  void OnDeactivate(Rml::TextInputContext *input_context) override {
    if (InputContext != input_context)
      return;
    // Intra-window refocus leaves the platform IME composing into a dead
    // target - cancel it outright so the next keystroke starts clean.
    cancelComposition();
    InputContext = nullptr;
  }
  void OnDestroy(Rml::TextInputContext *input_context) override {
    if (InputContext != input_context)
      return;
    // The widget (and its context) is being destroyed - drop pointers
    // only. A mid-composition destruction leaves the composing bytes in
    // the dead editor's value; deactivation always precedes this for the
    // hide/close paths.
    endComposition();
    InputContext = nullptr;
  }

  bool hasActiveEditor() const {
    return InputContext != nullptr;
  }
  bool composing() const {
    return Composing;
  }

  // SDL_EVENT_TEXT_EDITING update: insert/replace the composing bytes and
  // place the caret at the platform-reported offset.
  void setComposition(std::string_view composition, int cursorCharacters) {
    if (!Composing)
      Composing = true;
    setCompositionString(composition);
    CursorCharacters = cursorCharacters;
    updateCursor();
    // Editors that cannot move the caret inside the composition (the
    // cursor offset stayed unknown) get the whole string selected instead
    // (updateCursor) and no range feedback.
    if (CursorCharacters >= 0 && InputContext)
      InputContext->SetCompositionRange(CompositionStart, CompositionEnd);
  }

  // SDL_EVENT_TEXT_INPUT commit during a composition: replace the
  // composing range with the final string under the editor's own
  // restrictions (maxlength, single-line filtering).
  void confirmComposition(std::string_view composition) {
    if (!Composing)
      return;
    setCompositionString(composition);
    if (InputContext) {
      InputContext->SetCompositionRange(CompositionStart, CompositionEnd);
      InputContext->CommitComposition(rmlView(composition));
    }
    endComposition();
  }

  // ESC-first / focus-loss / teardown: purge the composing bytes, restore
  // the caret and tell the platform to drop its half too.
  void cancelComposition() {
    if (!Composing)
      return;
    if (InputContext) {
      InputContext->SetText(Rml::StringView(), CompositionStart,
                            CompositionEnd);
      InputContext->SetCursorPosition(CompositionStart);
    }
    endComposition();
    platformCancel();
  }

  void reset() {
    endComposition();
    InputContext = nullptr;
  }

private:
  void endComposition() {
    if (InputContext)
      InputContext->SetCompositionRange(0, 0);
    Composing = false;
    CompositionStart = 0;
    CompositionEnd = 0;
    CursorCharacters = -1;
  }

  void setCompositionString(std::string_view composition) {
    if (!InputContext)
      return;
    // First update of a composition: the range starts as the current
    // selection (what the IME is about to replace).
    if (CompositionStart == 0 && CompositionEnd == 0)
      InputContext->GetSelectionRange(CompositionStart, CompositionEnd);
    InputContext->SetText(rmlView(composition), CompositionStart,
                          CompositionEnd);
    CompositionEnd =
        CompositionStart + static_cast<int>(Rml::StringUtilities::LengthUTF8(
                               rmlView(composition)));
  }

  void updateCursor() {
    if (!InputContext || (CompositionStart == 0 && CompositionEnd == 0))
      return;
    if (CursorCharacters >= 0)
      InputContext->SetCursorPosition(CompositionStart + CursorCharacters);
    else
      InputContext->SetSelectionRange(CompositionStart, CompositionEnd);
  }

  // Best-effort platform cancel (ImmNotifyIME CPS_CANCEL, the same call
  // VGUI2Extension's CInput2 uses): no global input-method state is
  // touched, the current composition of the game window is dropped.
  void platformCancel() {
    const HWND window = gameWindow();
    if (!window)
      return;
    if (const HIMC context = ImmGetContext(window)) {
      ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
      ImmReleaseContext(window, context);
    }
  }

  Rml::TextInputContext *InputContext = nullptr;
  bool Composing = false;
  int CompositionStart = 0;
  int CompositionEnd = 0;
  int CursorCharacters = -1;
};

// The game's top-level window handle, captured from the surface WndProc
// seam (noteSurfaceWindow) and resolved lazily otherwise (SDL owns the real
// HWND; the engine exposes no handle through the interfaces we bind).
HWND GameWindowHandle = nullptr;

// Diagnostic counters (T07 on-machine evidence; read via abc_ui_input_events).
// Cheap unconditional increments - a handful of integers, no formatting.
struct EventCounters {
  unsigned AppProcTotal = 0;       // every SDL event the AppProc forward saw
  unsigned AppProcTextInput = 0;   // SDL_EVENT_TEXT_INPUT seen
  unsigned AppProcTextEditing = 0; // SDL_EVENT_TEXT_EDITING seen
  unsigned AppProcCandidates = 0;  // SDL_EVENT_TEXT_EDITING_CANDIDATES seen
  unsigned WndProcTotal = 0;       // every message the surface WndProc saw
  unsigned WndProcChar = 0;        // WM_CHAR seen by the surface WndProc
  unsigned WndProcKey = 0;         // WM_KEYDOWN/UP seen
  unsigned FeedCommits = 0;        // commits submitted to an editor
  unsigned FeedDrops = 0;          // commits dropped (no focused editor)
  // Message-type histogram (all message ids, for channel discovery).
  unsigned WndProcTypes[256] = {};
  // SDL event-type histogram (low byte of the SDL_EventType).
  unsigned AppProcTypes[256] = {};
};
EventCounters Counters;

// T16 console toggle-key swallow latch (see swallowToggleKeyText).
bool SwallowToggleText = false;

HWND gameWindow() {
  if (GameWindowHandle && IsWindow(GameWindowHandle))
    return GameWindowHandle;
  GameWindowHandle = GetActiveWindow();
  if (GameWindowHandle)
    return GameWindowHandle;
  struct Search {
    DWORD processId;
    HWND found;
  } search = {GetCurrentProcessId(), nullptr};
  EnumWindows(
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto *hit = reinterpret_cast<Search *>(lparam);
        DWORD processId = 0;
        GetWindowThreadProcessId(hwnd, &processId);
        if (processId != hit->processId || !IsWindowVisible(hwnd) ||
            GetWindow(hwnd, GW_OWNER))
          return TRUE;
        hit->found = hwnd;
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&search));
  GameWindowHandle = search.found;
  return GameWindowHandle;
}

TextInputAdapter &adapter() {
  static TextInputAdapter instance;
  return instance;
}

bool isTextEditor(const Rml::Element *element) {
  return element &&
         (rmlui_dynamic_cast<const Rml::ElementFormControlInput *>(element) ||
          rmlui_dynamic_cast<const Rml::ElementFormControlTextArea *>(element));
}

// T07 step 2: only an editor with keyboard focus receives characters.
void submitToFocusedEditor(std::string_view text) {
  Rml::Context *context = rmlui::context();
  if (!context || !isTextEditor(context->GetFocusElement())) {
    Counters.FeedDrops++;
    return;
  }
  Counters.FeedCommits++;
  context->ProcessTextInput(Rml::String(text.data(), text.size()));
}

} // namespace

Rml::TextInputHandler &textInput() {
  return adapter();
}

bool textComposing() {
  return adapter().composing();
}

void noteSurfaceWindow(const void *hwnd) {
  // The WndProc callback hands the real surface window; the cached value
  // short-circuits the resolution in gameWindow().
  const HWND window = static_cast<HWND>(const_cast<void *>(hwnd));
  if (window && IsWindow(window))
    GameWindowHandle = window;
}

// IME placement (pin backend reference
// SystemInterface_Win32::ActivateKeyboard): composition box at the caret,
// candidate window excluding the caret line. CFS_FORCE_POSITION takes surface
// client pixels directly (the context pixel space, T06) - no scaling layer.
void updateImePlacement(Rml::Vector2f caretPosition, float lineHeight) {
  // The composition panel follows the caret (rmlui::ime); the vgui2 candidate
  // window is placed under it the way a VGUI text entry places it.
  ime::setAnchor(caretPosition.x, caretPosition.y, lineHeight);

  const HWND window = gameWindow();
  if (!window)
    return;
  const HIMC context = ImmGetContext(window);
  if (!context)
    return;
  // Candidate page: read it from the IME itself (the same CANDIDATELIST
  // vgui2's window renders). The SDL candidates event is NOT used - its
  // payload layout in this build does not match the header copy (live crash:
  // the field we took for the string-array pointer held the count).
  ime::feedCandidatesFromIme(context);
  constexpr LONG kBottomMargin = 2;
  const LONG x = static_cast<LONG>(caretPosition.x);
  const LONG y = static_cast<LONG>(caretPosition.y);

  COMPOSITIONFORM composition = {};
  composition.dwStyle = CFS_FORCE_POSITION;
  composition.ptCurrentPos = {x, y};
  ImmSetCompositionWindow(context, &composition);

  CANDIDATEFORM candidate = {};
  candidate.dwStyle = CFS_EXCLUDE;
  candidate.ptCurrentPos = {x, y};
  candidate.rcArea = {x, y, x + 1,
                      y + static_cast<LONG>(lineHeight) + kBottomMargin};
  ImmSetCandidateWindow(context, &candidate);

  ImmReleaseContext(window, context);
}

// Frame-time candidate refresh while composing. The event-driven feeds
// (SDL_EVENT_TEXT_EDITING_CANDIDATES, the ActivateKeyboard caret hook) are
// both unreliable on this install: the VGUI2Extension proxy supercedes the
// candidates event before plugins late in the chain, and an IME driver that
// owns the window messages can keep it from being emitted at all. The
// CANDIDATELIST stays readable from the IME context for the whole
// composition (VGUI2Extension's own candidate window reads it at event time
// on this very install), so polling once per imeState read covers every
// configuration. Cheap and bounded: only while one of our editors composes.
void refreshCompositionCandidates() {
  if (!adapter().composing())
    return;
  const HWND window = gameWindow();
  if (!window)
    return;
  if (const HIMC context = ImmGetContext(window)) {
    ime::feedCandidatesFromIme(context);
    ImmReleaseContext(window, context);
  }
}

void feedTextInput(const char *utf8) {
  const std::string_view sanitized = framework::text::sanitizeUtf8(utf8);
  if (sanitized.empty())
    return;
  // T16: the console toggle key's own commit is dropped (see the header
  // note); any other commit clears a stale arm.
  if (SwallowToggleText) {
    SwallowToggleText = false;
    if (sanitized == "`" || sanitized == "~")
      return;
  }
  if (adapter().composing()) {
    adapter().confirmComposition(sanitized);
    ime::hide();
  } else {
    submitToFocusedEditor(sanitized);
  }
}

void swallowToggleKeyText() {
  SwallowToggleText = true;
}

void feedTextEditing(const char *utf8, int startBytes) {
  // Composition is editor-scoped: without one of our focused editors the
  // update belongs to the original surface's fields.
  if (!adapter().hasActiveEditor())
    return;
  const std::string_view sanitized = framework::text::sanitizeUtf8(utf8);
  if (sanitized.empty()) {
    // Empty composition while tracking = the platform cancelled it
    // (ESC) or cleared it post-commit (already ended -> no-op above).
    adapter().cancelComposition();
    ime::hide();
    return;
  }
  // The overlay header shows the running composition (the editor shows the
  // same string inline, underlined); the candidate page arrives separately.
  ime::setComposition(std::string(sanitized).c_str(), startBytes);
  // The cursor offset is a byte offset inside the composition string
  // (SDL contract); Rml's caret is a character index.
  const int clamped =
      std::clamp(startBytes, 0, static_cast<int>(sanitized.size()));
  const int cursor =
      framework::text::countCharacters(sanitized.substr(0, clamped));
  adapter().setComposition(sanitized, cursor);
}

void cancelComposition() {
  adapter().cancelComposition();
  ime::hide();
}

void onWindowFocusLost() {
  // State-only: the OS cancels the platform composition when the window
  // deactivates; the mirrored Rml state must not survive into the next
  // focus period.
  adapter().reset();
  ime::hide();
}

void onSurfaceAppEvent(const void *sdlEvent) {
  if (!sdlEvent)
    return;
  const unsigned type = *static_cast<const unsigned *>(sdlEvent);
  Counters.AppProcTotal++;
  Counters.AppProcTypes[type & 0xFF]++;
  switch (type) {
  case kSdlEventTextInput:
    Counters.AppProcTextInput++;
    feedTextInput(static_cast<const SdlTextInputEvent *>(sdlEvent)->Text);
    break;
  case kSdlEventTextEditing: {
    Counters.AppProcTextEditing++;
    const auto *editing = static_cast<const SdlTextEditingEvent *>(sdlEvent);
    feedTextEditing(editing->Text, editing->Start);
    break;
  }
  case kSdlEventTextEditingCandidates: {
    Counters.AppProcCandidates++;
    // Layout identification first, decisions after: the payload shape has
    // flipped between engine builds, so every event is dumped raw (the
    // probe prints it) and both known layouts are probed with full
    // validation before anything is dereferenced.
    const auto *raw = static_cast<const unsigned *>(sdlEvent);
    ime::noteCandidatesPayload(raw);
    if (!adapter().hasActiveEditor())
      break;
    // Crash-safe pointer validation (the first live attempt dereferenced
    // the count field as the array pointer).
    struct Memory {
      static bool readable(const void *address, size_t bytes) {
        if (!address)
          return false;
        MEMORY_BASIC_INFORMATION info = {};
        if (!VirtualQuery(address, &info, sizeof(info)))
          return false;
        if (info.State != MEM_COMMIT)
          return false;
        const DWORD protect = info.Protect & 0xFF;
        const bool readablePage =
            protect == PAGE_READONLY || protect == PAGE_READWRITE ||
            protect == PAGE_WRITECOPY || protect == PAGE_EXECUTE_READ ||
            protect == PAGE_EXECUTE_READWRITE ||
            protect == PAGE_EXECUTE_WRITECOPY;
        if (!readablePage)
          return false;
        const auto base = reinterpret_cast<uintptr_t>(info.BaseAddress);
        return reinterpret_cast<uintptr_t>(address) + bytes <=
               base + info.RegionSize;
      }
    };
    const auto validShape = [&](const void *array, int count, int selected) {
      const size_t arrayBytes = static_cast<size_t>(count) * sizeof(void *);
      return count > 0 && count <= 32 && selected >= -1 && selected <= 32 &&
             Memory::readable(array, arrayBytes) &&
             Memory::readable(*static_cast<const void *const *>(array), 1);
    };
    // Layout A (the VGUI2Extension proxy declares this one): array pointer,
    // count, selection.
    const char *const *arrayA = reinterpret_cast<const char *const *>(raw[3]);
    const int countA = static_cast<int>(raw[4]);
    const int selectedA = static_cast<int>(raw[5]);
    if (validShape(arrayA, countA, selectedA)) {
      ime::feedCandidatesFromEvent(arrayA, countA, selectedA);
      break;
    }
    // Layout B (the count-first build): count, array pointer, selection.
    const int countB = static_cast<int>(raw[3]);
    const char *const *arrayB = reinterpret_cast<const char *const *>(raw[4]);
    const int selectedB = static_cast<int>(raw[5]);
    if (validShape(arrayB, countB, selectedB)) {
      ime::feedCandidatesFromEvent(arrayB, countB, selectedB);
      break;
    }
    // Explicit close (both layouts agree on null pointer + zero count) or a
    // payload this build does not carry strings in.
    if (!arrayA && countA == 0) {
      ime::clearCandidates();
      break;
    }
    // Legacy IMM32 IMEs publish the page through the context instead.
    if (const HWND window = gameWindow()) {
      if (const HIMC context = ImmGetContext(window)) {
        ime::feedCandidatesFromIme(context);
        ImmReleaseContext(window, context);
      }
    }
    break;
  }
  default:
    break; // keys/mouse/wheel ride the T06 event sources
  }
}

void noteWndProcMessage(unsigned msg) {
  Counters.WndProcTotal++;
  if (msg < 256)
    Counters.WndProcTypes[msg]++;
  if (msg == 0x0102)
    Counters.WndProcChar++;
  else if (msg == 0x0100 || msg == 0x0101)
    Counters.WndProcKey++;
}

void eventCounters(unsigned &appProcTotal, unsigned &appProcTextInput,
                   unsigned &appProcTextEditing, unsigned &appProcCandidates,
                   unsigned &wndProcTotal, unsigned &wndProcChar,
                   unsigned &wndProcKey, unsigned &feedCommits,
                   unsigned &feedDrops) {
  appProcTotal = Counters.AppProcTotal;
  appProcTextInput = Counters.AppProcTextInput;
  appProcTextEditing = Counters.AppProcTextEditing;
  appProcCandidates = Counters.AppProcCandidates;
  wndProcTotal = Counters.WndProcTotal;
  wndProcChar = Counters.WndProcChar;
  wndProcKey = Counters.WndProcKey;
  feedCommits = Counters.FeedCommits;
  feedDrops = Counters.FeedDrops;
}

void messageHistogram(unsigned *wndProcTypes, unsigned *appProcTypes) {
  for (int i = 0; i < 256; ++i) {
    wndProcTypes[i] = Counters.WndProcTypes[i];
    appProcTypes[i] = Counters.AppProcTypes[i];
  }
}

bool compositionOwnsKey(int keynum) {
  if (!textComposing())
    return false;
  return keynum == K_ENTER || keynum == K_KP_ENTER || keynum == K_ESCAPE ||
         keynum == K_BACKSPACE;
}

void textInputShutdown() {
  adapter().reset();
}

} // namespace rmlui
