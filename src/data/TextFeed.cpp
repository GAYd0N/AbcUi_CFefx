// data::textfeed implementation - see TextFeed.h.
// from: ABCEnchance/src/hud/Viewport.cpp g_EventTextMsg handler (GBK->UTF-8).
// Port note: the old handler built its printf calls with the format/size
// arguments transposed (Q_snprintf(buffer, msg, sstr1) with V_snprintf
// semantics = destLen garbage, sstr1 as format). The port uses the standard
// TextMsg contract: msg is the format, sstr1..4 the arguments.

#include "TextFeed.h"

#include <regex>

#include <algorithm>
#include <cstdio>

#include <abc/Format.h>

#include "NetworkMessages.h"

#include "../framework/Localization.h"

// Windows.h min/max macros fight std::min (old repo precedent).
#undef max
#undef min

namespace textfeed {

namespace {

// Old killfeed patterns (std::wregex before; UTF-8 byte matching is
// equivalent for these ASCII patterns).
const std::regex &suicidePattern() {
  static const std::regex pattern(" committed suicide.");
  return pattern;
}
const std::regex &killedPattern() {
  static const std::regex pattern(" was killed by a ");
  return pattern;
}
const std::regex &playerPattern() {
  static const std::regex pattern(" : (.*) : ");
  return pattern;
}

std::string matchPrefix(const std::smatch &match) {
  return match.prefix().str();
}

// Leading-# tokens resolve through the plugin language table (the table keys
// are stored bare - the same convention as AbcSystemInterface::TranslateString,
// which strips the '#'). The old findLocalize never translated anything (its
// assignment targeted a by-value parameter); the old Viewport handed the
// token to vgui's localize Find, which covered the game resource tables too -
// no game-table fallback here (D-17 note: the plugin ships its own keys).
std::string findLocalize(const std::string &token) {
  if (token.empty() || token.front() != '#')
    return token;
  std::string translated;
  if (framework::Localization::translate(translated, token.substr(1)))
    return translated;
  return token;
}

// D-17 #2 (R-17 closure, abc::Format round): the TextMsg protocol hands the
// client a printf-style format plus arguments, and the old localize tables
// mixed in the vgui %s1..%s4 positional style - both went into sprintf_s,
// where a stray %n / specifier mismatch crashed the HUD. The text is
// normalized into {n} placeholders and substituted through abc::Format, so
// the server text is never a printf format string.
//
// Positional fix (D-17 quirk correction): the old code condensed the params
// to the non-empty prefix and passed those in order, losing position (a
// format "%s %s" with params {"", "B"} rendered "B "). The port substitutes
// positionally - {n} is always params[n] - matching the vgui %s1..%s4
// semantics the templates were written against.
//
// Residual semantics change (D-17 note): a server message that carries its
// own literal "{0}" text AND at least one param now has that text replaced
// (old printf paths only reacted to '%'). abc::Format renders out-of-range
// placeholders verbatim, so only in-range indices are affected - accepted as
// the price of never interpreting server text as a format string.
std::string normalizePlaceholders(const std::string &fmt) {
  std::string out;
  out.reserve(fmt.size());
  int index = 0;
  for (size_t i = 0; i < fmt.size(); i++) {
    const char c = fmt[i];
    if (c != '%') {
      out += c;
      continue;
    }
    if (i + 1 < fmt.size() && fmt[i + 1] == 's') {
      // vgui ConstructString positional form: %s1..%s9.
      if (i + 2 < fmt.size() && fmt[i + 2] >= '1' && fmt[i + 2] <= '9') {
        out += '{';
        out += static_cast<char>(fmt[i + 2] - 1); // '1' -> index 0
        out += '}';
        i += 2;
      }
      // printf sequential form: each %s takes the next position.
      else {
        out += '{';
        out += static_cast<char>('0' + std::min(index, 9));
        out += '}';
        index++;
        i++;
      }
    } else if (i + 1 < fmt.size() && fmt[i + 1] == '%') {
      out += '%';
      i++;
    } else {
      // Stray %: literal (was a crash/UB vector under printf).
      out += '%';
    }
  }
  return out;
}

// Formats the notice text: #token resolution on msg + params, then the
// normalized msg substituted through abc::Format with the full parameter
// vector (see the positional note above).
std::string formatNotice(std::string msg, std::string params[4]) {
  msg = findLocalize(msg);
  for (int i = 0; i < 4; i++)
    params[i] = findLocalize(params[i]);
  const std::string text = abc::Format(normalizePlaceholders(msg), params[0],
                                       params[1], params[2], params[3]);
  std::string out = text;
  if (!out.empty() && out.back() == '\n')
    out.pop_back();
  std::replace(out.begin(), out.end(), '\r', '\n');
  return out;
}

int onTextMsg(int target, const char *msg, const char *param1,
              const char *param2, const char *param3, const char *param4) {
  // Unknown game tokens stay with the native handler (safe game-table
  // fallback).
  if (msg && msg[0] == '#') {
    std::string translated;
    if (!framework::Localization::translate(translated, std::string(msg + 1)))
      return true;
  }
  if (target == kPrintNotify) {
    if (msg[0] == '\0')
      return true;
    const std::string text = msg;
    std::smatch match;
    const auto localized = [](const char *token) {
      std::string out;
      framework::Localization::translate(out, token);
      return out;
    };
    if (std::regex_search(text, match, suicidePattern())) {
      KillFeed(matchPrefix(match).c_str(), "",
               localized("DeathMsg_Suicide").c_str());
      return false;
    }
    if (std::regex_search(text, match, killedPattern())) {
      std::string killer = match.suffix().str();
      // Old: k.erase(k.end() - 2) erased only the second-to-last char
      // (the '.'), leaving the '\n' (old quirk, fixed per D-17): the
      // port trims the whole ".\n" tail.
      if (killer.size() >= 2)
        killer.erase(killer.size() - 2);
      KillFeed(matchPrefix(match).c_str(), killer.c_str(),
               localized("DeathMsg_MonsterKill").c_str());
      return false;
    }
    if (std::regex_search(text, match, playerPattern())) {
      // Old: inflictor = the " : X : " in-between group, attacker =
      // prefix, victim = suffix.
      KillFeed(match.suffix().str().c_str(), matchPrefix(match).c_str(),
               match[1].str().c_str());
      return false;
    }
  }

  if (target != kPrintNotify && target != kPrintCenter)
    return true;

  std::string params[4] = {param1, param2, param3, param4};
  const std::string formatted = formatNotice(msg, params);
  if (formatted.empty())
    return true;
  Notice(target, formatted.c_str());
  return false;
}

} // namespace

abc::CallbackList<void(const char *, const char *, const char *)> KillFeed;
abc::CallbackList<void(int, const char *)> Notice;

void init() {
  static bool registered = false;
  if (registered)
    return;
  registered = true;
  KillFeed = {};
  Notice = {};
  netmsg::TextMsg.append(onTextMsg);
}

} // namespace textfeed
