// rmlui::ime implementation - see ImeOverlay.h.
#include "ImeOverlay.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <Windows.h>
#include <imm.h>

#include <abc/Console.h>

#include "../framework/TextUtil.h"

namespace rmlui {
namespace ime {

namespace {

constexpr const char *kDocumentPath = "abc/abcui/hud/rml/ime.rml";
// Above every page tier (pages 0, loading 500, connection dialogs 550): the
// composition UI belongs to the caret, never under a dialog.
constexpr const char *kZIndex = "600";
// Panel metrics in dp, mirroring hud/rcss/ime.rcss. They size the clamp and
// the flip-above rule before the first layout of a fresh page exists.
constexpr float kRowHeightDp = 15.0f;
constexpr float kHeaderHeightDp = 13.0f;
constexpr float kPanelPaddingDp = 5.0f;
constexpr float kPanelWidthPerCharDp = 9.0f;
constexpr float kMaxPanelWidthDp = 180.0f;

Rml::Context *Context = nullptr;
Rml::ElementDocument *Document = nullptr;

std::string Composition;
int CompositionCursor = 0;
std::vector<std::string> Candidates;
int Selected = -1;
bool Horizontal = false;

// Caret anchor in surface pixels (ActivateKeyboard), plus the density the
// context is running at so the panel can be placed in dp.
float AnchorX = 0.0f;
float AnchorY = 0.0f;
float LineHeight = 0.0f;
bool AnchorValid = false;

std::string dpValue(float value) {
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "%.2fdp", value);
  return buffer;
}

// UTF-8 code points, not bytes: a CJK row is three bytes per glyph and the
// clamp would otherwise push the panel far further left than it needs.
std::size_t textLength(const std::string &text) {
  std::size_t characters = 0;
  for (unsigned char byte : text) {
    if ((byte & 0xC0) != 0x80)
      characters++;
  }
  return characters;
}

// Rough width of the widest row: the clamp only needs to keep the panel on
// screen, and being a little generous costs nothing.
float estimatedPanelWidthDp() {
  std::size_t widest = textLength(Composition);
  for (const auto &candidate : Candidates)
    widest = std::max(widest, textLength(candidate));
  return std::min(kMaxPanelWidthDp,
                  kPanelPaddingDp * 2.0f +
                      static_cast<float>(widest) * kPanelWidthPerCharDp);
}

void position() {
  if (!Document || !Context)
    return;
  Rml::Element *root = Document->GetElementById("ime");
  if (!root)
    return;
  if (!AnchorValid)
    return;
  float density = Context->GetDensityIndependentPixelRatio();
  if (!(density > 0.0f))
    density = 1.0f;
  const Rml::Vector2i dimensions = Context->GetDimensions();

  const float caretX = AnchorX / density;
  const float caretY = AnchorY / density;
  const float lineHeight = LineHeight / density;

  const float width = estimatedPanelWidthDp();
  const float height = kPanelPaddingDp * 2.0f +
                       (Composition.empty() ? 0.0f : kHeaderHeightDp) +
                       static_cast<float>(Candidates.size()) * kRowHeightDp;

  float left = caretX;
  const float maxLeft = static_cast<float>(dimensions.x) - width - 4.0f;
  left = std::clamp(left, 4.0f, std::max(4.0f, maxLeft));
  float top = caretY + lineHeight + 2.0f;
  if (top + height > static_cast<float>(dimensions.y) - 4.0f)
    top = caretY - height - 2.0f; // flip above the composition line
  top = std::max(4.0f, top);

  root->SetProperty("left", dpValue(left));
  root->SetProperty("top", dpValue(top));
}

void rebuild() {
  if (!Document)
    return;
  const bool visible = !Composition.empty() || !Candidates.empty();
  if (!visible) {
    Document->Hide();
    return;
  }

  std::string rml;
  if (!Composition.empty()) {
    rml += "<div id=\"ime-composition\">";
    rml += framework::TextUtil::escapeRml(Composition);
    rml += "</div>";
  }
  if (!Candidates.empty()) {
    rml += "<div id=\"ime-candidates\"";
    rml += Horizontal ? " class=\"horizontal\">" : ">";
    for (std::size_t i = 0; i < Candidates.size(); i++) {
      // The selected row is the one the IME would commit on Enter; the index
      // the platform reports is relative to the page. Every row carries the
      // key the IME listens for (1..9), so the player can pick by number.
      rml += "<div class=\"ime-candidate";
      if (static_cast<int>(i) == Selected)
        rml += " selected";
      rml += "\"><span class=\"ime-index\">";
      rml += std::to_string(i + 1);
      rml += "</span><span class=\"ime-text\">";
      rml += framework::TextUtil::escapeRml(Candidates[i]);
      rml += "</span></div>";
    }
    rml += "</div>";
  }
  if (Rml::Element *panel = Document->GetElementById("ime-panel"))
    panel->SetInnerRML(rml);
  if (!Document->IsVisible())
    Document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
  position();
}

} // namespace

void init(Rml::Context *context) {
  if (!context || Document)
    return;
  Context = context;
  Document = context->LoadDocument(kDocumentPath);
  if (!Document) {
    abc::ConsoleWarn("[AbcUi] IME overlay: failed to load %s\n", kDocumentPath);
    return;
  }
  // The overlay is a caret decoration: it must never take the pointer or the
  // keyboard from the focused editor.
  Document->SetProperty("pointer-events", "none");
  Document->SetProperty("z-index", kZIndex);
  Document->Hide();
}

void shutdown() {
  if (Document) {
    Document->Close();
    Document = nullptr;
  }
  Context = nullptr;
  Composition.clear();
  CompositionCursor = 0;
  Candidates.clear();
  Selected = -1;
  Horizontal = false;
  AnchorValid = false;
}

void setComposition(const char *utf8, int cursorBytes) {
  Composition = utf8 ? utf8 : "";
  CompositionCursor = std::clamp(cursorBytes, 0, (int)Composition.size());
  if (Composition.empty())
    Candidates.clear(); // a commit/cancel ends the page with it
  rebuild();
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
  if (context) {
    const DWORD bytes = ImmGetCandidateListW(context, 0, nullptr, 0);
    if (bytes > sizeof(CANDIDATELIST)) {
      std::vector<unsigned char> block(bytes, 0);
      auto *list = reinterpret_cast<LPCANDIDATELIST>(block.data());
      if (ImmGetCandidateListW(context, 0, list, bytes)) {
        const DWORD count = list->dwCount;
        DWORD start = list->dwPageStart;
        DWORD pageSize = list->dwPageSize ? list->dwPageSize : count;
        if (start >= count)
          start = 0;
        // Nine rows, not ten: the IME selects with 1..9, so a tenth entry
        // would carry a key label the user cannot press.
        const DWORD end = std::min(count, start + std::min<DWORD>(pageSize, 9));
        for (DWORD i = start; i < end; i++) {
          const DWORD offset = list->dwOffset[i];
          if (offset == 0 || offset >= bytes)
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
  Candidates = std::move(page);
  Selected = std::clamp(selected, -1, static_cast<int>(Candidates.size()) - 1);
  Horizontal = horizontal;
  rebuild();
}

} // namespace

void feedCandidatesFromIme(void *himc) {
  applyPlatformCandidates(static_cast<HIMC>(himc));
}

void setAnchor(float xPixels, float yPixels, float lineHeightPixels) {
  AnchorX = xPixels;
  AnchorY = yPixels;
  LineHeight = lineHeightPixels;
  AnchorValid = true;
  position();
}

void hide() {
  Composition.clear();
  CompositionCursor = 0;
  Candidates.clear();
  Selected = -1;
  Horizontal = false;
  if (Document)
    Document->Hide();
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

} // namespace ime
} // namespace rmlui
