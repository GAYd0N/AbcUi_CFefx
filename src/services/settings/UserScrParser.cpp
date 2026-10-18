// user.scr parser - extracted from SettingsModel.cpp. The grammar is the
// original CInfoDescription format; the function stays a member so it can use
// the model's strict parseValue helper, but it no longer lives in the
// draft/apply TU.
#include "SettingsModel.h"

namespace services {
namespace settings {

namespace {

// Minimal quoted-string scanner for the user.scr grammar: skips whitespace,
// reads one "..." token or one bare word/brace. Returns false at end of
// input; `quoted` reports whether the token came from quotes.
bool scrToken(const char *&p, std::string &out, bool &quoted) {
  out.clear();
  quoted = false;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
    ++p;
  if (*p == '\0')
    return false;
  if (*p == '"') {
    quoted = true;
    ++p;
    while (*p && *p != '"') {
      if (*p == '\\' && p[1])
        ++p; // keep the escaped byte verbatim (shipped files carry none)
      out.push_back(*p++);
    }
    if (*p == '"')
      ++p;
    return true;
  }
  if (*p == '{' || *p == '}') {
    out.push_back(*p++);
    return true;
  }
  while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' &&
         *p != '{' && *p != '}')
    out.push_back(*p++);
  return true;
}

std::string stripComment(const std::string &line) {
  const auto pos = line.find("//");
  return pos == std::string::npos ? line : line.substr(0, pos);
}

} // namespace

// --- user.scr parser ---------------------------------------------------------
//
// The original CInfoDescription grammar (the header comment in the shipped
// file is the spec):
//
//   "cvar" { "Prompt" { TYPE [type info] } { default } }
//   TYPE = BOOL | NUMBER min max | STRING | LIST "label" "value" ...
//
// Comments start with "//". Entries with unknown types or missing pieces are
// skipped (the original parser is similarly tolerant); a parse that yields
// no rows reports false and the caller keeps the static fallback table.

bool SettingsModel::parseUserScr(const char *text, UserScrTable &out) {
  out = UserScrTable{};
  if (!text)
    return false;
  // The grammar is brace-oriented, not line-oriented; flatten with comment
  // lines stripped so the token walk below stays simple.
  std::string flat;
  {
    std::string line;
    for (const char *p = text;; ++p) {
      const char c = *p;
      if (c == '\n' || c == '\0') {
        flat += stripComment(line);
        flat += '\n';
        line.clear();
        if (c == '\0')
          break;
      } else if (line.size() < 8192) {
        line.push_back(c);
      }
    }
  }

  enum State {
    SeekDescription,  // before the DESCRIPTION token
    SeekOpenBrace,    // after DESCRIPTION, waiting for the block '{'
    EntryCvar,        // next quoted token is the entry cvar
    EntryBlockOpen,   // waiting for the entry block '{'
    EntryPrompt,      // next quoted token is the prompt
    TypeBlockOpen,    // waiting for the type block '{'
    TypeBlock,        // inside { TYPE ... } (first token = the type word)
    DefaultBlockOpen, // waiting for the default block '{'
    DefaultBlock,     // inside { default }
  };
  State state = SeekDescription;
  std::string token;
  bool quoted = false;
  std::string cvar, prompt, type, defaultValue;
  std::string pendingLabel;
  std::vector<float> numberParts;
  std::vector<SettingEnumOption> listOptions;
  float numberMin = 0.0f, numberMax = 0.0f;

  auto resetEntry = [&]() {
    cvar.clear();
    prompt.clear();
    type.clear();
    defaultValue.clear();
    listOptions.clear();
    pendingLabel.clear();
    numberParts.clear();
    numberMin = 0.0f;
    numberMax = 0.0f;
  };

  auto storeRow = [&]() {
    if (cvar.empty() || prompt.empty() || type.empty() || defaultValue.empty())
      return;
    if (out.Rows.size() >= 64)
      return; // fail-closed cap: a modded script cannot explode the page
    SettingRow stored{};
    float parsedDefault = 0.0f;
    if (type == "BOOL") {
      stored.Kind = SettingKind::kBool;
      stored.Min = 0.0f;
      stored.Max = 1.0f;
      stored.Step = 1.0f;
      parseValue(defaultValue.c_str(), parsedDefault);
      stored.DefaultValue = parsedDefault;
    } else if (type == "NUMBER") {
      stored.Kind = SettingKind::kFloat;
      stored.Min = numberParts.size() >= 1 ? numberParts[0] : 0.0f;
      stored.Max = numberParts.size() >= 2 ? numberParts[1] : 0.0f;
      stored.Step = 0.0f;
      stored.Bounded = !(stored.Min == -1 && stored.Max == -1);
      parseValue(defaultValue.c_str(), parsedDefault);
      stored.DefaultValue = parsedDefault;
    } else if (type == "LIST") {
      stored.Kind = SettingKind::kEnum;
      parseValue(defaultValue.c_str(), parsedDefault);
      stored.DefaultValue = parsedDefault;
    } else if (type == "STRING") {
      stored.Kind = SettingKind::kText;
      stored.DefaultValue = 0.0f;
    } else {
      return; // unknown type: skip the entry like the original parser
    }
    out.Strings.push_back(cvar);
    out.Strings.push_back(prompt);
    out.Strings.push_back(defaultValue);
    stored.Cvar = out.Strings[out.Strings.size() - 3].c_str();
    stored.LabelToken = out.Strings[out.Strings.size() - 2].c_str();
    stored.DefaultString = out.Strings[out.Strings.size() - 1].c_str();
    stored.Section = "#Settings_SecAdvanced";
    if (!listOptions.empty()) {
      out.Options.push_back(listOptions);
      stored.Options = out.Options.back().data();
      stored.OptionCount = static_cast<int>(out.Options.back().size());
    }
    out.Rows.push_back(stored);
  };

  const char *p = flat.c_str();
  while (scrToken(p, token, quoted)) {
    if (token == "{") {
      switch (state) {
      case SeekOpenBrace:
        state = EntryCvar;
        break;
      case EntryBlockOpen:
        state = EntryPrompt;
        break;
      case TypeBlockOpen:
        state = TypeBlock;
        break;
      case DefaultBlockOpen:
        state = DefaultBlock;
        break;
      default:
        break; // no nested blocks in the grammar; ignore extras
      }
      continue;
    }
    if (token == "}") {
      switch (state) {
      case TypeBlock:
        if (type == "NUMBER") {
          numberMin = numberParts.size() >= 1 ? numberParts[0] : 0.0f;
          numberMax = numberParts.size() >= 2 ? numberParts[1] : 0.0f;
        }
        state = DefaultBlockOpen;
        break;
      case DefaultBlock:
        storeRow();
        resetEntry();
        state = EntryCvar;
        break;
      case EntryCvar:
      case EntryBlockOpen:
      case TypeBlockOpen:
      case DefaultBlockOpen:
        // A block closed early: drop the partial entry, resync to
        // the description block (entries may follow).
        resetEntry();
        state = EntryCvar;
        break;
      default:
        break;
      }
      continue;
    }
    if (state == SeekDescription) {
      if (token == "DESCRIPTION")
        state = SeekOpenBrace;
      continue;
    }
    if (state == SeekOpenBrace)
      continue; // the description token ("INFO_OPTIONS")
    switch (state) {
    case EntryCvar:
      if (quoted) {
        cvar = token;
        state = EntryBlockOpen;
      }
      break;
    case EntryBlockOpen:
      break; // tolerate stray tokens before the block brace
    case EntryPrompt:
      if (quoted) {
        prompt = token;
        state = TypeBlockOpen;
      }
      break;
    case TypeBlockOpen:
      break;
    case TypeBlock:
      if (type.empty()) {
        type = token;
      } else if (type == "LIST") {
        // "Label" "value" pairs (both tokens arrive quoted in the
        // shipped files): the first of the pair is the label, the
        // second the value; the label anchors into the arena (the
        // strings must outlive the model - the row keeps raw
        // pointers into it).
        if (pendingLabel.empty()) {
          pendingLabel = token;
        } else {
          float value = 0.0f;
          parseValue(token.c_str(), value);
          out.Strings.push_back(pendingLabel);
          listOptions.push_back({value, out.Strings.back().c_str()});
          pendingLabel.clear();
        }
      } else {
        float number = 0.0f;
        if (parseValue(token.c_str(), number))
          numberParts.push_back(number);
      }
      break;
    case DefaultBlock:
      if (quoted && defaultValue.empty())
        defaultValue = token;
      break;
    default:
      break;
    }
  }
  return !out.Rows.empty();
}

} // namespace settings
} // namespace services
