// rmlui::KeyMap implementation - see KeyMap.h.
#include "KeyMap.h"

#include <keydefs.h>

namespace rmlui {

namespace {

// SDL_SCANCODE_TO_KEYCODE's mask (thirdparty SDL3_fork SDL_keycode.h:
// SDLK_SCANCODE_MASK = 1 << 30; SDL2 uses the same scheme).
constexpr int kSdlScancodeMask = 0x40000000;

// SDL scancodes (stable across SDL2/SDL3; SDL3_fork SDL_scancode.h).
constexpr int kSdlScancodeReturn = 40;
constexpr int kSdlScancodeEscape = 41;
constexpr int kSdlScancodeBackspace = 42;
constexpr int kSdlScancodeTab = 43;
constexpr int kSdlScancodeSpace = 44;
constexpr int kSdlScancodeCapslock = 57;
constexpr int kSdlScancodeF1 = 58;
constexpr int kSdlScancodeF12 = 69;
constexpr int kSdlScancodePause = 72;
constexpr int kSdlScancodeInsert = 73;
constexpr int kSdlScancodeHome = 74;
constexpr int kSdlScancodePageup = 75;
constexpr int kSdlScancodeDelete = 76;
constexpr int kSdlScancodeEnd = 77;
constexpr int kSdlScancodePagedown = 78;
constexpr int kSdlScancodeRight = 79;
constexpr int kSdlScancodeLeft = 80;
constexpr int kSdlScancodeDown = 81;
constexpr int kSdlScancodeUp = 82;
constexpr int kSdlScancodeKpDivide = 84;
constexpr int kSdlScancodeKpMinus = 86;
constexpr int kSdlScancodeKpPlus = 87;
constexpr int kSdlScancodeKpEnter = 88;
constexpr int kSdlScancodeKp1 = 89;
constexpr int kSdlScancodeKp0 = 98;
constexpr int kSdlScancodeKpPeriod = 99;
constexpr int kSdlScancodeLCtrl = 224;
constexpr int kSdlScancodeLShift = 225;
constexpr int kSdlScancodeLAlt = 226;
constexpr int kSdlScancodeRCtrl = 228;
constexpr int kSdlScancodeRShift = 229;
constexpr int kSdlScancodeRAlt = 230;

} // namespace

Rml::Input::KeyIdentifier mapKeyCode(int keynum) {
  // Normal keys arrive as lowercased ASCII (keydefs.h note).
  if (keynum >= 'a' && keynum <= 'z')
    return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_A +
                                                  (keynum - 'a'));
  if (keynum >= '0' && keynum <= '9')
    return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_0 +
                                                  (keynum - '0'));

  switch (keynum) {
  case K_SPACE:
    return Rml::Input::KI_SPACE;
  case K_TAB:
    return Rml::Input::KI_TAB;
  case K_ENTER:
    return Rml::Input::KI_RETURN;
  case K_ESCAPE:
    return Rml::Input::KI_ESCAPE;
  case K_BACKSPACE:
    return Rml::Input::KI_BACK;
  case K_UPARROW:
    return Rml::Input::KI_UP;
  case K_DOWNARROW:
    return Rml::Input::KI_DOWN;
  case K_LEFTARROW:
    return Rml::Input::KI_LEFT;
  case K_RIGHTARROW:
    return Rml::Input::KI_RIGHT;
  case K_INS:
    return Rml::Input::KI_INSERT;
  case K_DEL:
    return Rml::Input::KI_DELETE;
  case K_PGDN:
    return Rml::Input::KI_NEXT;
  case K_PGUP:
    return Rml::Input::KI_PRIOR;
  case K_HOME:
    return Rml::Input::KI_HOME;
  case K_END:
    return Rml::Input::KI_END;
  case K_CAPSLOCK:
    return Rml::Input::KI_CAPITAL;
  case K_KP_SLASH:
    return Rml::Input::KI_DIVIDE;
  case K_KP_MINUS:
    return Rml::Input::KI_SUBTRACT;
  case K_KP_PLUS:
    return Rml::Input::KI_ADD;
  case K_KP_ENTER:
    return Rml::Input::KI_NUMPADENTER;
  case K_KP_INS:
    return Rml::Input::KI_NUMPAD0;
  case K_KP_END:
    return Rml::Input::KI_NUMPAD1;
  case K_KP_DOWNARROW:
    return Rml::Input::KI_NUMPAD2;
  case K_KP_PGDN:
    return Rml::Input::KI_NUMPAD3;
  case K_KP_LEFTARROW:
    return Rml::Input::KI_NUMPAD4;
  case K_KP_5:
    return Rml::Input::KI_NUMPAD5;
  case K_KP_RIGHTARROW:
    return Rml::Input::KI_NUMPAD6;
  case K_KP_HOME:
    return Rml::Input::KI_NUMPAD7;
  case K_KP_UPARROW:
    return Rml::Input::KI_NUMPAD8;
  case K_KP_PGUP:
    return Rml::Input::KI_NUMPAD9;
  // The engine delivers the wheel as keynum down-pulses (no matching up),
  // which has no Rml key equivalent - UNKNOWN drops them until wheel
  // scrolling is routed as real wheel events (input batch C).
  case K_MWHEELDOWN:
    return Rml::Input::KI_UNKNOWN;
  case K_MWHEELUP:
    return Rml::Input::KI_UNKNOWN;
  default:
    break;
  }

  if (keynum >= K_F1 && keynum <= K_F12)
    return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_F1 +
                                                  (keynum - K_F1));

  return Rml::Input::KI_UNKNOWN;
}

int mapSdlKeycode(int keycode) {
  // Printable ASCII identity (see the header contract): the character key
  // space the engine itself uses. Uppercase folds to the lowercase engine
  // keynum the binding/capture side and mapKeyCode speak.
  if (keycode >= 'a' && keycode <= 'z')
    return keycode;
  if (keycode >= 'A' && keycode <= 'Z')
    return keycode - 'A' + 'a';
  if (keycode >= ' ' && keycode <= '~')
    return keycode; // letters, digits, space, punctuation
  // The control characters SDL keeps below 0x20 plus DELETE: the engine's
  // editing keynums (its own table crosses SDL/engine backspace and delete,
  // see mapKeyCode's note).
  switch (keycode) {
  case 0x08:
    return K_BACKSPACE; // SDLK_BACKSPACE
  case 0x09:
    return K_TAB; // SDLK_TAB
  case 0x0D:
    return K_ENTER; // SDLK_RETURN
  case 0x1B:
    return K_ESCAPE; // SDLK_ESCAPE
  case 0x7F:
    return K_DEL; // SDLK_DELETE
  default:
    break;
  }

  if ((keycode & kSdlScancodeMask) == 0)
    return -1; // not a printable key and not an SDL scancode keycode

  const int scancode = keycode & ~kSdlScancodeMask;
  if (scancode >= kSdlScancodeF1 && scancode <= kSdlScancodeF12)
    return K_F1 + (scancode - kSdlScancodeF1);
  switch (scancode) {
  case kSdlScancodeReturn:
    return K_ENTER;
  case kSdlScancodeEscape:
    return K_ESCAPE;
  case kSdlScancodeBackspace:
    return K_BACKSPACE;
  case kSdlScancodeTab:
    return K_TAB;
  case kSdlScancodeSpace:
    return K_SPACE;
  case kSdlScancodeCapslock:
    return K_CAPSLOCK;
  case kSdlScancodePause:
    return K_PAUSE;
  case kSdlScancodeInsert:
    return K_INS;
  case kSdlScancodeHome:
    return K_HOME;
  case kSdlScancodePageup:
    return K_PGUP;
  case kSdlScancodeDelete:
    return K_DEL;
  case kSdlScancodeEnd:
    return K_END;
  case kSdlScancodePagedown:
    return K_PGDN;
  case kSdlScancodeRight:
    return K_RIGHTARROW;
  case kSdlScancodeLeft:
    return K_LEFTARROW;
  case kSdlScancodeDown:
    return K_DOWNARROW;
  case kSdlScancodeUp:
    return K_UPARROW;
  case kSdlScancodeKpDivide:
    return K_KP_SLASH;
  case kSdlScancodeKpMinus:
    return K_KP_MINUS;
  case kSdlScancodeKpPlus:
    return K_KP_PLUS;
  case kSdlScancodeKpEnter:
    return K_KP_ENTER;
  // The keypad's positional keynums (the engine names them by the NumLock-
  // off legends; KeyMap's mapKeyCode turns them back into KI_NUMPAD0..9).
  case kSdlScancodeKp1:
    return K_KP_END;
  case kSdlScancodeKp1 + 1:
    return K_KP_DOWNARROW;
  case kSdlScancodeKp1 + 2:
    return K_KP_PGDN;
  case kSdlScancodeKp1 + 3:
    return K_KP_LEFTARROW;
  case kSdlScancodeKp1 + 4:
    return K_KP_5;
  case kSdlScancodeKp1 + 5:
    return K_KP_RIGHTARROW;
  case kSdlScancodeKp1 + 6:
    return K_KP_HOME;
  case kSdlScancodeKp1 + 7:
    return K_KP_UPARROW;
  case kSdlScancodeKp1 + 8:
    return K_KP_PGUP;
  case kSdlScancodeKp0:
    return K_KP_INS;
  case kSdlScancodeKpPeriod:
    return K_KP_DEL;
  case kSdlScancodeLCtrl:
  case kSdlScancodeRCtrl:
    return K_CTRL;
  case kSdlScancodeLShift:
  case kSdlScancodeRShift:
    return K_SHIFT;
  case kSdlScancodeLAlt:
  case kSdlScancodeRAlt:
    return K_ALT;
  default:
    break;
  }
  return -1;
}

} // namespace rmlui
