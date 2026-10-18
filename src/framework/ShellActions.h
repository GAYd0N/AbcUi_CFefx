#pragma once

// framework shell actions - the small set of OS-shell calls the UI needs.
// Keeping them behind a named helper keeps platform headers out of panels.

#include <string>

namespace framework {

void OpenExternalUrl(const std::string &url);

} // namespace framework
