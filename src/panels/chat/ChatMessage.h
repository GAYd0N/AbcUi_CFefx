#pragma once

// panels::chat::message - one SayText line turned into styled runs (issue
// 13).
//
// The server composes the whole line; the only structure on the wire is the
// engine's colour control bytes plus, on HLSDK-shaped servers, positional
// %s / %sN substitution strings. This module turns that into runs the panel
// can paint, with no Rml and no engine reads, so the shape rules stay
// reviewable in one place.
//
// Control bytes (HLSDK hud_saytext.cpp):
//   0x01 TEXTCOLOR_NORMAL       - default text colour follows
//   0x02 TEXTCOLOR_USEOLDCOLORS - the sender's name inside the text is tinted
//   0x03 TEXTCOLOR_PLAYERNAME   - the run after it is the sender name
//   0x04 TEXTCOLOR_LOCATION     - location/team marker colour
// Everything outside those bytes is literal text; the PANEL escapes it
// before it reaches the DOM, never this module.
//
// The team/dead/spectator prefixes the issue asks for are the client's own
// decorations on the composed line ("(TEAM) ", "*DEAD* ", "*SPEC* "): they
// are split into their own run so the panel can tint them without parsing
// player names.
//
// from: CaptionMod src/chatdialog.cpp (CChatDialogLine::InsertAndColorizeText
// + CChatDialog::ChatPrintf), reshaped to run values instead of vgui colour
// ranges.

#include <string>
#include <vector>

namespace panels {
namespace chat {

enum class RunKind {
  kText = 0, // ordinary message text
  kName,     // the sender's name (tinted with the engine player colour)
  kPrefix,   // "(TEAM) " / "*DEAD* " / "*SPEC* " decoration
  kLocation, // location / team marker run
};

struct Run {
  std::string text;
  RunKind kind = RunKind::kText;
};

// Builds the runs for one SayText line. `params` are the four optional
// substitution strings (all empty when the server composed the line itself);
// `senderName` is the sender's player name, used only by the legacy
// "use old colours" marker to find the name inside the text. Returns an
// empty vector when nothing displayable is left (a bare control byte, an
// empty line).
std::vector<Run> buildRuns(const std::string &text,
                           const std::string (&params)[4],
                           const std::string &senderName);

} // namespace chat
} // namespace panels
