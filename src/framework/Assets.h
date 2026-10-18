#pragma once

// framework::Assets - abc/abcui/ asset install checks + shared path prefixes.
// Plan 05 §1: assets live in <mod>/svencoop/abc/abcui/. GameUI documents
// and their page-specific art/config live under gameui/, HUD documents and
// their page-specific art/sprites live under hud/, and the cross-surface
// stylesheets, language tables and shared art stay at the abcui root.
// File reads go through the engine IFileSystem, so this module only owns
// existence checks and the canonical prefix constants.

namespace framework {

namespace Assets {

// Canonical asset-root prefix as referenced from RML/RCSS.
constexpr const char *kRootPrefix = "abc/abcui/";

// LoadClient time: verify the installed assets, warn (once) about missing
// ones (plugin loads even without them).
void verifyInstalled();

} // namespace Assets

} // namespace framework
