#include "ImeOverlay.h"

#include <Windows.h>
#include <imm.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace rmlui::ime {
namespace {
struct State {
  std::string composition;
  int cursor = 0;
  std::vector<std::string> candidates;
  int selected = -1;
  bool horizontal = false;
  float x = 0, y = 0, lineHeight = 0;
  bool anchorValid = false;
};
State &state() {
  static State instance;
  return instance;
}

// abc_ui_input_events diagnostics: how many candidate reads ran and what the
// last one saw (0 bytes = the IME context carried no list at read time;
// eventCount -1 = the SDL payload was absent or failed validation).
unsigned DebugFeeds = 0;
unsigned DebugLastBytes = 0;
unsigned DebugEventFeeds = 0;
int DebugLastEventCount = -1;
// Raw payload capture for the layout identification dump.
unsigned DebugPayloadSeen = 0;
unsigned DebugPayloadWords[8] = {};
} // namespace

void noteCandidatesPayload(const unsigned *words) {
  DebugPayloadSeen++;
  for (int i = 0; i < 8; i++)
    DebugPayloadWords[i] = words[i];
}

void imePayloadDebug(unsigned &seen, unsigned *words) {
  seen = DebugPayloadSeen;
  if (words)
    for (int i = 0; i < 8; i++)
      words[i] = DebugPayloadWords[i];
}

void imeDebug(unsigned &feeds, unsigned &lastBytes, unsigned &eventFeeds,
             int &lastEventCount, int &count, int &selected, bool &anchorValid,
             int &compositionBytes) {
  const auto &s = state();
  feeds = DebugFeeds;
  lastBytes = DebugLastBytes;
  eventFeeds = DebugEventFeeds;
  lastEventCount = DebugLastEventCount;
  count = static_cast<int>(s.candidates.size());
  selected = s.selected;
  anchorValid = s.anchorValid;
  compositionBytes = static_cast<int>(s.composition.size());
}

void shutdown() {
  state() = {};
}
void setComposition(const char *utf8, int cursorBytes) {
  auto &s = state();
  s.composition = utf8 ? utf8 : "";
  s.cursor = std::clamp(cursorBytes, 0, static_cast<int>(s.composition.size()));
  if (s.composition.empty())
    s.candidates.clear();
}

namespace {

// Copies the IME's own candidate page into the overlay rows. The CANDIDATELIST
// carries UTF-16 strings at byte offsets from the block start, a page window
// (dwPageStart/dwPageSize) and the selection index in list coordinates - the
// same data vgui2's candidate window renders.
void applyPlatformCandidates(HIMC context) {
  std::vector<std::string> page;
  int selected = -1;
  // One horizontal row, like the game's own IME window: the page is short
  // (<= 10 entries) and a row keeps the panel from covering the play area.
  constexpr bool horizontal = true;
  DebugFeeds++;
  DebugLastBytes = 0;
  if (context) {
    const DWORD bytes = ImmGetCandidateListW(context, 0, nullptr, 0);
    DebugLastBytes = bytes;
    if (bytes > sizeof(CANDIDATELIST) && bytes <= 1024 * 1024) {
      std::vector<unsigned char> block(bytes, 0);
      auto *list = reinterpret_cast<LPCANDIDATELIST>(block.data());
      if (ImmGetCandidateListW(context, 0, list, bytes)) {
        const DWORD capacity =
            (bytes - offsetof(CANDIDATELIST, dwOffset)) / sizeof(DWORD);
        const DWORD count = std::min(list->dwCount, capacity);
        DWORD start = list->dwPageStart;
        DWORD pageSize = list->dwPageSize ? list->dwPageSize : count;
        if (start >= count)
          start = 0;
        // Nine rows, not ten: the IME selects with 1..9, so a tenth entry
        // would carry a key label the user cannot press.
        const DWORD end =
            start + std::min(count - start, std::min<DWORD>(pageSize, 9));
        for (DWORD i = start; i < end; i++) {
          const DWORD offset = list->dwOffset[i];
          if (offset == 0 || offset % sizeof(wchar_t) || offset >= bytes)
            continue;
          const auto *text =
              reinterpret_cast<const wchar_t *>(block.data() + offset);
          // Bounded read: the block belongs to the IME, but a truncated
          // terminator must not walk past its end.
          int characters = 0;
          while (offset + (characters + 1) * sizeof(wchar_t) < bytes &&
                 text[characters] != 0)
            characters++;
          if (characters == 0)
            continue;
          const int needed = WideCharToMultiByte(CP_UTF8, 0, text, characters,
                                                 nullptr, 0, nullptr, nullptr);
          if (needed <= 0)
            continue;
          std::string utf8(static_cast<std::size_t>(needed), 0);
          WideCharToMultiByte(CP_UTF8, 0, text, characters, utf8.data(), needed,
                              nullptr, nullptr);
          utf8.resize(static_cast<std::size_t>(needed));
          page.push_back(std::move(utf8));
        }
        if (list->dwSelection >= start)
          selected = static_cast<int>(list->dwSelection - start);
      }
    }
  }
  state().candidates = std::move(page);
  state().selected =
      std::clamp(selected, -1, static_cast<int>(state().candidates.size()) - 1);
  state().horizontal = horizontal;
}

} // namespace

void feedCandidatesFromIme(void *himc) {
  applyPlatformCandidates(static_cast<HIMC>(himc));
}

// The SDL candidates event carries the page the IME's TSF side extracted
// (SDL3-style payload): UTF-8 strings, the selection index and nothing else -
// on TSF-driven IMEs the IMM32 CANDIDATELIST this process can read stays
// empty, so this payload is the authoritative source when present.
void feedCandidatesFromEvent(const char *const *candidates, int count,
                             int selected) {
  std::vector<std::string> page;
  int selection = -1;
  DebugEventFeeds++;
  DebugLastEventCount = count;
  if (candidates)
    for (int i = 0; i < count && static_cast<int>(page.size()) < 16; i++)
      if (candidates[i] && candidates[i][0])
        page.emplace_back(candidates[i]);
  if (selected >= 0 && selected < static_cast<int>(page.size()))
    selection = selected;
  auto &s = state();
  s.candidates = std::move(page);
  s.selected = selection;
  s.horizontal = true;
}

void clearCandidates() {
  auto &s = state();
  s.candidates.clear();
  s.selected = -1;
}

void setAnchor(float x, float y, float lineHeight) {
  auto &s = state();
  s.x = x;
  s.y = y;
  s.lineHeight = lineHeight;
  s.anchorValid = true;
}
void hide() {
  auto &s = state();
  s.composition.clear();
  s.cursor = 0;
  s.candidates.clear();
  s.selected = -1;
}
void snapshot(AbcUiImeState &out) {
  const auto &s = state();
  out = {};
  out.structSize = sizeof(out);
  out.visible =
      s.anchorValid && (!s.composition.empty() || !s.candidates.empty());
  out.cursorBytes = s.cursor;
  out.x = s.x;
  out.y = s.y;
  out.lineHeight = s.lineHeight;
  out.selected = s.selected;
  out.candidateCount = std::min(9, static_cast<int>(s.candidates.size()));
  const auto copy = [](const std::string &value, char *target, size_t size) {
    // Do not split a UTF-8 character at the fixed-capacity ABI boundary.
    size_t n = std::min(value.size(), size - 1);
    while (n < value.size() && n &&
           (static_cast<unsigned char>(value[n]) & 0xc0) == 0x80)
      --n;
    std::memcpy(target, value.data(), n);
    target[n] = 0;
  };
  copy(s.composition, out.composition, sizeof(out.composition));
  for (int i = 0; i < out.candidateCount; ++i)
    copy(s.candidates[i], out.candidates[i], sizeof(out.candidates[i]));
}

std::string inputLanguageTag() {
  // The game thread's layout is the one the focused editor types with; a
  // plain keyboard layout (no IME) answers empty, which hides the badge.
  const HKL layout = GetKeyboardLayout(0);
  if (!layout || !ImmIsIME(layout))
    return {};
  const LANGID id = LOWORD(layout);
  switch (PRIMARYLANGID(id)) {
  case LANG_CHINESE:
    // Traditional/香港 layouts keep their own tag; everything else is
    // simplified Chinese (the common case: Microsoft Pinyin).
    switch (SUBLANGID(id)) {
    case SUBLANG_CHINESE_TRADITIONAL:
      return "TW";
    case SUBLANG_CHINESE_HONGKONG:
      return "HK";
    case SUBLANG_CHINESE_SINGAPORE:
      return "SG";
    default:
      return "CN";
    }
  case LANG_JAPANESE:
    return "JP";
  case LANG_KOREAN:
    return "KR";
  default:
    return {};
  }
}

} // namespace rmlui::ime
