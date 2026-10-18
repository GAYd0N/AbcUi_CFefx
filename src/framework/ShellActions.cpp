// framework::OpenExternalUrl implementation - see ShellActions.h.
#include "ShellActions.h"

#include <windows.h>

#include <shellapi.h>

namespace framework {

void OpenExternalUrl(const std::string &url) {
  if (url.empty())
    return;
  ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace framework
