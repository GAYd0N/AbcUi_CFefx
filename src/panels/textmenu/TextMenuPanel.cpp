// panels::TextMenuPanel implementation - see TextMenuPanel.h.
// from: ABCEnchance/src/hud/vgui/textmenu.* (GBK->UTF-8).

#include "TextMenuPanel.h"
#include "../../framework/TextUtil.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>
#include <format>

#include <abc/CvarCmd.h>
#include <abc/Console.h>
#include <abc/Events.h>

#include "../../data/NetworkMessages.h"
#include "../../framework/Commands.h"

namespace panels {

namespace textmenu {

// Logical open state (old CTextMenu::IsVisible). The panel instance keeps
// writing it in sync; bus consumers (WeaponChoose slot feed) read isOpen().

abc::CChainedCommand *command(int index) {
  if (index < 0 || index > 9)
    return nullptr;
  const std::string name = std::format("slot{}", index + 1);
  return commands::get(name.c_str());
}

bool isOpen() {
  return commands::textMenuOpen();
}

} // namespace textmenu

namespace {
// Old StartFade sequences TextMenuIn/Out: 0.2s alpha, visibility flips.
constexpr double kFadeSeconds = 0.2;

// Acceptance-driver visibility: name every state transition on the stdout
// mirror so a menu that dies without an explicit closer identifies itself
// (abc_ui_probe instrument role; compiled out in Release).
#ifndef NDEBUG
void traceTransition(const char *what, double detail, double now) {
  abc::ConsolePrint("[AbcUi] textmenu: %s (detail=%.3f now=%.3f)\n", what,
                    detail, now);
}
#else
void traceTransition(const char *, double, double) {}
#endif

void fadeTo(Rml::Element *body, bool state) {
  if (!body)
    return;
  body->SetProperty("transition", "opacity 0.2s linear-in");
  body->SetProperty("opacity", state ? "1" : "0");
}

// Old TextImageEx::Paint inline colour stream (SC 5.26 menu text): \w \d \y
// \r \c \g \m \o switch colour mid-string, \R toggles right-aligned flow,
// a newline resets to the label colour. Colours byte-exact from the old
// switch; \d is the label fg itself (TextMenu.TextColor #dedede).
const char *menuColorCode(char code) {
  switch (code) {
  case 'w':
    return "#ffffff";
  case 'd':
    return "#dedede";
  case 'y':
    return "#ffd240"; // 255 210 64
  case 'r':
    return "#d24000"; // 210 64 0
  case 'c':
    return "#00ffff";
  case 'g':
    return "#00ff00";
  case 'm':
    return "#ff00ff";
  case 'o':
    return "#ffa500"; // 255 165 0
  default:
    return nullptr;
  }
}

std::string colorizeMenuRml(const std::string &text) {
  std::string out;
  out.reserve(text.size() + 32);
  bool inSpan = false;
  const auto closeSpan = [&]() {
    if (inSpan) {
      out += "</span>";
      inSpan = false;
    }
  };
  for (size_t i = 0; i < text.size(); i++) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == '\\' && i + 1 < text.size()) {
      if (const char *hex = menuColorCode(text[i + 1])) {
        closeSpan();
        out += "<span style=\"color: ";
        out += hex;
        out += "\">";
        inSpan = true;
        i++;
        continue;
      }
      if (text[i + 1] == 'R') {
        // Right-align flow control: no inline-markup equivalent in
        // Rml; consume so it does not print (server menus rarely
        // ship it - old Paint mirrored the x cursor).
        i++;
        continue;
      }
    }
    if (c == '\r' || c <= 8) {
      // Old Paint: CR and control chars <= 8 are skipped.
      continue;
    }
    if (c == '\n') {
      // Old Paint resets to the label colour per line. The break must be
      // a <br/>: a raw newline lands between spans, and the parser drops
      // whitespace-only text there regardless of white-space (found live
      // via the abc_ui_menu injector - the menu painted as one line).
      closeSpan();
      out += "<br/>";
      continue;
    }
    if (c == '&') {
      // "&&" draws one ampersand; a single & was the hotkey marker
      // (old code skipped it, the next char still drew).
      if (i + 1 < text.size() && text[i + 1] == '&') {
        out += "&amp;";
        i++;
      }
      continue;
    }
    switch (c) {
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    default:
      out += c;
    }
  }
  closeSpan();
  return out;
}
} // namespace

TextMenuPanel::TextMenuPanel()
    : HudPanel("textmenu", "abc/abcui/hud/rml/textmenu.rml") {}

TextMenuPanel::~TextMenuPanel() {
  commands::setTextMenuOpen(false);
}

void TextMenuPanel::onDocumentReady() {
  // Invisible until a ShowMenu arrives (opacity 0, not display:none - the
  // fade-out transition needs the element alive).
  if (Rml::Element *body = getDocument())
    fadeTo(body, false);

  subscribe(netmsg::ShowMenu, [this](int validSlots, int displaySeconds,
                                     int bits, const std::string &text) {
    showMenu(validSlots, displaySeconds, bits, text);
  });
  subscribe(netmsg::InitHud, [this]() { reset(); });
  // Old g_EventCmdSlot handler: the menu eats slot keys while open; the
  // weapon-slot chain only runs when it is closed.
  for (int i = 0; i < 10; i++) {
    if (abc::CChainedCommand *chained = textmenu::command(i)) {
      subscribe(chained->Event(), [this, i]() {
        if (!commands::textMenuOpen())
          return true;
        selectMenuItem(i + 1);
        return false;
      });
    }
  }
}

void TextMenuPanel::showMenu(int validSlots, int displaySeconds, int bits,
                             const std::string &text) {
  // Old MsgShowMenu.
  if (!WaitingForMore)
    MenuString.clear();
  ValidSlots = validSlots;
  const int needMore = bits & 0x7F;
  IsASMenu = ((bits >> 7) & 1) != 0;
  ShutoffTime = displaySeconds > 0 ? ClientTimeNow + displaySeconds : -1.0;
  if (ValidSlots) {
    if (text.size() > 16384 || MenuString.size() > 16384 - text.size()) {
      MenuString.clear();
      reset();
      return;
    }
    MenuString += text;
    if (needMore == 0) {
      // Old: strip the leading/trailing newline runs servers send.
      MenuString = framework::TextUtil::trimNewlines(MenuString);
      if (Rml::Element *menu = element("menu"))
        menu->SetInnerRML(colorizeMenuRml(MenuString));
      if (Rml::Element *body = getDocument())
        fadeTo(body, true);
      show();
      commands::setTextMenuOpen(true);
      // A fresh menu cancels a pending fade-out-hide (old StartFade(true)
      // ran CancelAnimationsForPanel first): the server's answer to a
      // menuselect usually lands inside the 0.2s window, and letting the
      // stale deadline fire would close the just-opened submenu.
      HideAtTime = -1.0;
      traceTransition("open", static_cast<double>(ValidSlots), ClientTimeNow);
    }
  } else {
    commands::setTextMenuOpen(false);
    if (Rml::Element *body = getDocument())
      fadeTo(body, false);
    // The document stays up for the fade-out; hide it once the opacity
    // transition has finished (OnFadeOutDone).
    HideAtTime = ClientTimeNow + kFadeSeconds;
    traceTransition("close-message", static_cast<double>(ValidSlots),
                    ClientTimeNow);
  }
  WaitingForMore = needMore != 0;
}

void TextMenuPanel::selectMenuItem(int slot) {
  // Old SelectMenuItem.
  if (slot > 0 && (ValidSlots & (1 << (slot - 1)))) {
    const std::string command = IsASMenu
                                    ? std::format("as_menuselect {}\n", slot)
                                    : std::format("menuselect {}\n", slot);
    abc::EngineClientCmd(command.c_str());
    commands::setTextMenuOpen(false);
    if (Rml::Element *body = getDocument())
      fadeTo(body, false);
    HideAtTime = ClientTimeNow + kFadeSeconds;
    traceTransition("select", static_cast<double>(slot), ClientTimeNow);
  }
}

void TextMenuPanel::onData(double frameTime) {
  ClientTimeNow += frameTime;
  // Old OnThink shutoff.
  if (ShutoffTime >= 0.0 && ClientTimeNow >= ShutoffTime) {
    commands::setTextMenuOpen(false);
    if (Rml::Element *body = getDocument())
      fadeTo(body, false);
    HideAtTime = ClientTimeNow + kFadeSeconds;
    ShutoffTime = -1.0;
    traceTransition("shutoff", ShutoffTime, ClientTimeNow);
  }
  // Fade-out finished: take the document out of the layout (old
  // TextMenuOut SetVisible 0 at +0.2s). Doing it here - instead of
  // keeping the document permanently shown - is what keeps slot-key
  // chaining (and the old IsTextMenuOpen gates) honest.
  if (HideAtTime >= 0.0 && ClientTimeNow >= HideAtTime) {
    HideAtTime = -1.0;
    hide();
    traceTransition("hide", ShutoffTime, ClientTimeNow);
  }
}

void TextMenuPanel::reset() {
  // Old Reset.
  MenuString.clear();
  ShutoffTime = -1.0;
  WaitingForMore = false;
  IsASMenu = false;
  ValidSlots = 0;
  commands::setTextMenuOpen(false);
  HideAtTime = -1.0;
  if (Rml::Element *menu = element("menu"))
    menu->SetInnerRML("");
  if (Rml::Element *body = getDocument())
    fadeTo(body, false);
  hide();
}

} // namespace panels
