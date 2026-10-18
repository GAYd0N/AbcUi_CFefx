// framework::CurrentLanguage implementation - see framework/Language.h.
#include "framework/Language.h"

#include "VGUI2ExtensionImport.h"

namespace framework {

const char *CurrentLanguage() {
  return vgui2ext::currentLanguage();
}

} // namespace framework
