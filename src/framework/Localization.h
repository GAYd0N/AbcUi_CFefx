#pragma once

// framework::Localization - token -> string table for the game language.
// Plan 05 §4.4: replaces ABCEnchance abcenchance/localize/{lang}.txt (VGUI
// localized format: "token" "value" pairs, GBK for the old schinese file) --
// the table files under abc/abcui/lang/ are UTF-8 `key = value` lines.
//
// Language selection: VGUI2Extension GetCurrentLanguage() when present,
// falling back to english (the old code's gamedir heuristic covers the same
// two cases in practice). Rml resolves "#token" strings through
// AbcSystemInterface::TranslateString -> translate() below.

#include <string>

namespace framework {

class Localization {
public:
  // Loads abc/abcui/lang/<lang>.txt for the detected language. Idempotent:
  // calling again after a language switch reloads the table.
  static void load();

  // True and fills `out` when the token is known; leaves `out` alone and
  // returns false otherwise.
  static bool translate(std::string &out, const std::string &token);

private:
  Localization() = default;
};

} // namespace framework
