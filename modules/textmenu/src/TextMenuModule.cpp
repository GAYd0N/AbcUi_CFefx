// abcui.textmenu - vanilla ShowMenu replacement (number-key menus). Moved
// from src/panels/textmenu (old ABCEnchance textmenu.*): chunked ShowMenu
// accumulation (bits&0x7f = more-chunks pending, bit7 = ammo-selection
// menu), shutoff countdown, the inline colour stream repaint and the
// slot1..10 key interception - while the menu is open the chained slot
// commands turn into menuselect and the engine weapon-slot path only runs
// while the menu is closed. The shared number-menu-open flag rides the
// commands api (the weaponchoose module gates on it).
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};

void panelReset(void *userdata);

// Old StartFade sequences TextMenuIn/Out: 0.2s alpha, visibility flips.
constexpr double kFadeSeconds = 0.2;

// Menu state (the ported panel members).
int ValidSlots = 0;
bool IsASMenu = false;
bool WaitingForMore = false;
std::string MenuString;
double ShutoffTime = -1.0;
double HideAtTime = -1.0;
double ClientTimeNow = 0.0;

void fadeTo(bool state) {
  AbcUiDocument *doc = Api->ui->panelDocument("textmenu");
  if (!doc)
    return;
  Api->ui->documentSetProperty(doc, "transition", "opacity 0.2s linear-in");
  Api->ui->documentSetProperty(doc, "opacity", state ? "1" : "0");
}

void setMenuOpen(bool open) {
  Api->commands->setTextMenuOpen(open ? 1 : 0);
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
        // Right-align flow control: no inline-markup equivalent in Rml;
        // consume so it does not print (server menus rarely ship it).
        i++;
        continue;
      }
    }
    if (c == '\r' || c <= 8) {
      // Old Paint: CR and control chars <= 8 are skipped.
      continue;
    }
    if (c == '\n') {
      // The break must be a <br/>: a raw newline lands between spans and
      // the parser drops whitespace-only text there regardless of
      // white-space (found live via the abc_ui_menu injector).
      closeSpan();
      out += "<br/>";
      continue;
    }
    if (c == '&') {
      // "&&" draws one ampersand; a single & was the hotkey marker (old
      // code skipped it, the next char still drew).
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

// Old TextUtil::trimNewlines: strip the leading/trailing newline runs
// servers send.
std::string trimNewlines(const std::string &text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && (text[begin] == '\n' || text[begin] == '\r'))
    begin++;
  while (end > begin && (text[end - 1] == '\n' || text[end - 1] == '\r'))
    end--;
  return text.substr(begin, end - begin);
}

void setMenuInnerRml(const char *rml) {
  AbcUiDocument *doc = Api->ui->panelDocument("textmenu");
  if (!doc)
    return;
  if (AbcUiElement *menu = Api->ui->querySelector(doc, "menu"))
    Api->ui->elementSetInnerRML(menu, rml);
}

void closeForFade() {
  // The document stays up for the fade-out; hide it once the opacity
  // transition has finished (old OnFadeOutDone at +0.2s).
  fadeTo(false);
  HideAtTime = ClientTimeNow + kFadeSeconds;
}

// Old MsgShowMenu.
void showMenu(int validSlots, int displaySeconds, int bits, const char *text) {
  if (!WaitingForMore)
    MenuString.clear();
  ValidSlots = validSlots;
  const int needMore = bits & 0x7F;
  IsASMenu = ((bits >> 7) & 1) != 0;
  ShutoffTime = displaySeconds > 0 ? ClientTimeNow + displaySeconds : -1.0;
  const size_t textSize = text ? strlen(text) : 0;
  if (ValidSlots) {
    if (textSize > 16384 || MenuString.size() > 16384 - textSize) {
      MenuString.clear();
      panelReset(nullptr);
      return;
    }
    MenuString += text;
    if (needMore == 0) {
      MenuString = trimNewlines(MenuString);
      const std::string rml = colorizeMenuRml(MenuString);
      setMenuInnerRml(rml.c_str());
      fadeTo(true);
      Api->ui->documentShow(Api->ui->panelDocument("textmenu"));
      setMenuOpen(true);
      // A fresh menu cancels a pending fade-out-hide: the server's answer
      // to a menuselect usually lands inside the 0.2s window, and letting
      // the stale deadline fire would close the just-opened submenu.
      HideAtTime = -1.0;
    }
  } else {
    setMenuOpen(false);
    closeForFade();
  }
  WaitingForMore = needMore != 0;
}

// Old SelectMenuItem: valid slot -> menuselect/as_menuselect + fade out.
void selectMenuItem(int slot) {
  if (slot > 0 && (ValidSlots & (1 << (slot - 1)))) {
    char command[32];
    std::snprintf(command, sizeof(command), "%s %d\n",
                  IsASMenu ? "as_menuselect" : "menuselect", slot);
    Api->console->engineCommand(command);
    setMenuOpen(false);
    closeForFade();
  }
}

void panelDocumentReady(void *) {
  // Invisible until a ShowMenu arrives (opacity 0, not display:none - the
  // fade-out transition needs the element alive).
  fadeTo(false);
}

void panelOnData(void *, double frameTime) {
  ClientTimeNow += frameTime;
  // Old OnThink shutoff.
  if (ShutoffTime >= 0.0 && ClientTimeNow >= ShutoffTime) {
    setMenuOpen(false);
    closeForFade();
    ShutoffTime = -1.0;
  }
  // Fade-out finished: take the document out of the layout. Doing it here -
  // instead of keeping the document permanently shown - is what keeps
  // slot-key chaining honest.
  if (HideAtTime >= 0.0 && ClientTimeNow >= HideAtTime) {
    HideAtTime = -1.0;
    Api->ui->documentHide(Api->ui->panelDocument("textmenu"));
  }
}

void panelReset(void *) {
  // Old Reset.
  MenuString.clear();
  ShutoffTime = -1.0;
  WaitingForMore = false;
  IsASMenu = false;
  ValidSlots = 0;
  setMenuOpen(false);
  HideAtTime = -1.0;
  setMenuInnerRml("");
  fadeTo(false);
  Api->ui->documentHide(Api->ui->panelDocument("textmenu"));
}

// ---- event sinks ----

void onShowMenu(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiShowMenuEvent *>(payload);
  showMenu(event->validSlots, event->displaySeconds, event->bits, event->text);
}

void onInitHud(void *, const void *) {
  panelReset(nullptr);
}

// Old g_EventCmdSlot handler: the menu eats slot keys while open; the
// weapon-slot chain only runs when it is closed (each subscriber decides
// independently - the aggregate veto keeps that shape).
int onSlotCommand(void *userdata, const void *) {
  const intptr_t slot = reinterpret_cast<intptr_t>(userdata);
  if (!Api->commands->textMenuOpen())
    return 0;
  selectMenuItem(static_cast<int>(slot) + 1);
  return 1;
}

// ---- lifecycle ----

void onModuleLoad() {
  Panel.structSize = sizeof(Panel);
  Panel.name = "textmenu";
  Panel.documentPath = "mod://abcui.textmenu/hud/rml/textmenu.rml";
  Panel.zIndex = 0;
  Panel.userdata = nullptr;
  Panel.onDocumentReady = &panelDocumentReady;
  Panel.onData = &panelOnData;
  Panel.onRedraw = nullptr;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);

  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_SHOW_MENU, 0, &onShowMenu,
                         nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_INIT_HUD, 0, &onInitHud, nullptr);
  for (intptr_t i = 0; i < 10; i++) {
    char name[8];
    std::snprintf(name, sizeof(name), "slot%d", static_cast<int>(i) + 1);
    Api->commands->subscribeCommand(name, 0, &onSlotCommand,
                                    reinterpret_cast<void *>(i));
  }
}

void onModuleShutdown() {
  // The panel dtor ran setTextMenuOpen(false); with the document gone the
  // flag must not stay set into the next cycle.
  setMenuOpen(false);
}

void onModuleUnload() {
  Api = nullptr;
}

} // namespace

ABCUI_MODULE_EXPORT int AbcUiModule_Main(uint32_t abiVersion,
                                         const AbcUiModuleApi *api,
                                         AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api || !out)
    return 0;
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->panels ||
      !api->events || !api->ui || !api->commands || !api->console)
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.textmenu";
  out->version = "1.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = nullptr;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
