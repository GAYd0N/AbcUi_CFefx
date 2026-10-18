#pragma once

// rmlui::AbcSystemInterface + AbcFileInterface - RmlUi platform seams.
// Plan 05 §3.3: elapsed time (QPC), logging (-> abc::Console levels),
// clipboard (Win32 CF_UNICODETEXT), localization hook; plus the file
// interface resolving RML/RCSS/image paths through the engine IFileSystem
// (the same mounted filesystem VGUI2Extension uses), not the process CWD.
//
// T07: ActivateKeyboard/DeactivateKeyboard position the platform IME
// (composition + candidate windows) at the editing caret - the pin's
// WidgetTextInput calls them on caret show/move/blur with the caret in
// context pixels, which is the surface client-pixel basis (T06 coordinate
// contract; no second scaling layer).

#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/SystemInterface.h>

namespace rmlui {

class AbcSystemInterface : public Rml::SystemInterface {
public:
  double GetElapsedTime() override;
  int TranslateString(Rml::String &translated,
                      const Rml::String &input) override;
  bool LogMessage(Rml::Log::Type type, const Rml::String &message) override;
  void SetClipboardText(const Rml::String &text) override;
  void GetClipboardText(Rml::String &text) override;
  // IME placement (T07 step 3). No-ops without a resolvable game window
  // (the composition stays at the platform default position then).
  void ActivateKeyboard(Rml::Vector2f caretPosition, float lineHeight) override;
  void DeactivateKeyboard() override;
};

class AbcFileInterface : public Rml::FileInterface {
public:
  Rml::FileHandle Open(const Rml::String &path) override;
  void Close(Rml::FileHandle file) override;
  size_t Read(void *buffer, size_t size, Rml::FileHandle file) override;
  bool Seek(Rml::FileHandle file, long offset, int origin) override;
  size_t Tell(Rml::FileHandle file) override;
};

} // namespace rmlui
