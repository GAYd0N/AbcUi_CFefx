// services::settings key-bind model implementation - see KeyBindModel.h.
#include "KeyBindModel.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace services {
namespace settings {

namespace {

// The GoldSrc key numbering puts ESCAPE at 27 (HLSDK keydefs.h; hw.dll's
// bind/unbind handlers refuse it). Guarded here too so the model stays
// correct even if a caller routes ESC into captureKey.
constexpr int kEscapeKeynum = 27;
// The bind handler joins argv[2..] into a 1KB stack buffer (hw.dll
// 0x01d71ae0): commands beyond this never fit and are refused up front.
constexpr size_t kMaxBindCommand = 1023;

// Trims space/tab/CR/LF around a token.
std::string trimCopy(const std::string &text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return {};
  return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

// One `"a" "b"` pair from a quoted-pair file (kb_act.lst / kb_def.lst /
// valve_english.txt). Returns false on lines without two quoted fields;
// '//' comments and blank lines are skipped. Values keep interior spaces.
bool parseQuotedPair(const std::string &line, std::string &first,
                     std::string &second) {
  const auto open1 = line.find('"');
  if (open1 == std::string::npos)
    return false;
  const auto close1 = line.find('"', open1 + 1);
  if (close1 == std::string::npos)
    return false;
  const auto open2 = line.find('"', close1 + 1);
  if (open2 == std::string::npos)
    return false;
  const auto close2 = line.find('"', open2 + 1);
  if (close2 == std::string::npos)
    return false;
  first = line.substr(open1 + 1, close1 - open1 - 1);
  second = line.substr(open2 + 1, close2 - open2 - 1);
  return true;
}

// Splits a file into lines (LF or CRLF), keeping interior characters.
std::vector<std::string> splitLines(const char *content) {
  std::vector<std::string> lines;
  if (!content)
    return lines;
  std::string current;
  for (const char *it = content; *it; ++it) {
    if (*it == '\n') {
      lines.push_back(current);
      current.clear();
    } else if (*it != '\r' && current.size() < 4096) {
      current.push_back(*it);
    }
  }
  if (!current.empty())
    lines.push_back(current);
  return lines;
}

// A key that appears in exactly one draft: engine truth guarantees it on
// snapshots, capture + conflict resolution + reset-defaults maintain it.
std::vector<int>::iterator findKey(std::vector<int> &keys, int keynum) {
  return std::ranges::find(keys, keynum);
}

bool hasKey(const std::vector<int> &keys, int keynum) {
  for (int existing : keys)
    if (existing == keynum)
      return true;
  return false;
}

// Drafts keep ascending keynum order - the same order the engine-table scan
// produces, so draft == engine compares equal after a confirmed write (and
// the page's key list renders deterministically).
void insertKey(std::vector<int> &keys, int keynum) {
  const auto itor = std::lower_bound(keys.begin(), keys.end(), keynum);
  keys.insert(itor, keynum);
}

} // namespace

bool KeyBindModel::loadActions(const char *content) {
  std::vector<BindAction> parsed;
  for (const auto &line : splitLines(content)) {
    std::string command, label;
    // kb_act.lst is all quoted pairs: a non-empty line that parses as
    // none means a corrupted file - refuse the whole table (fail-closed,
    // the page renders the section unavailable).
    if (!parseQuotedPair(line, command, label)) {
      if (trimCopy(line).empty())
        continue;
      return false;
    }
    BindAction action;
    action.Label = label;
    if (command == "blank") {
      action.Header = true; // section separators carry no command
    } else if (commandBindable(command)) {
      action.Command = command;
    } else {
      return false; // a kb_act.lst entry that could never bind safely
    }
    parsed.push_back(std::move(action));
  }
  Actions_ = std::move(parsed);
  Rows_.clear();
  Rows_.resize(Actions_.size());
  for (size_t i = 0; i < Actions_.size(); ++i)
    Rows_[i].action = &Actions_[i];
  return true;
}

bool KeyBindModel::loadDefaults(const char *content) {
  std::vector<std::pair<std::string, std::string>> parsed;
  for (const auto &line : splitLines(content)) {
    std::string key, command;
    if (!parseQuotedPair(line, key, command))
      continue;
    if (key.empty() || !commandBindable(command))
      continue;
    // Later duplicates replace earlier ones (the engine replays the
    // file in order; kb_def.lst itself repeats "u").
    bool replaced = false;
    for (auto &entry : parsed) {
      if (entry.first == key) {
        entry.second = command;
        replaced = true;
        break;
      }
    }
    if (!replaced)
      parsed.emplace_back(std::move(key), std::move(command));
  }
  Defaults_ = std::move(parsed);
  return true;
}

bool KeyBindModel::loadLabels(const char *content) {
  for (const auto &line : splitLines(content)) {
    std::string token, text;
    if (!parseQuotedPair(line, token, text) || token.empty())
      continue;
    bool replaced = false;
    for (auto &entry : Labels_) {
      if (entry.first == token) {
        entry.second = text;
        replaced = true;
        break;
      }
    }
    if (!replaced)
      Labels_.emplace_back(std::move(token), std::move(text));
  }
  return true;
}

bool KeyBindModel::lookupLabel(const std::string &token,
                               std::string &out) const {
  for (const auto &entry : Labels_) {
    if (entry.first == token) {
      out = entry.second;
      return true;
    }
  }
  return false;
}

std::string KeyBindModel::decodeResourceText(const char *data, size_t size) {
  if (!data || size == 0)
    return {};
  const auto *bytes = reinterpret_cast<const unsigned char *>(data);
  const bool littleEndian = size >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe;
  const bool bigEndian = size >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff;
  if (!littleEndian && !bigEndian)
    return std::string(data, size); // ASCII/UTF-8 source: verbatim

  auto readUnit = [&](size_t offset) -> unsigned { // UTF-16 code unit
    const unsigned lo = bytes[offset];
    const unsigned hi = bytes[offset + 1];
    return littleEndian ? (lo | (hi << 8)) : ((lo << 8) | hi);
  };
  auto appendUtf8 = [](std::string &out, unsigned code) {
    if (code < 0x80) {
      out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
      out.push_back(static_cast<char>(0xc0 | (code >> 6)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    } else if (code < 0x10000) {
      out.push_back(static_cast<char>(0xe0 | (code >> 12)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    } else {
      out.push_back(static_cast<char>(0xf0 | (code >> 18)));
      out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
  };
  std::string out;
  out.reserve(size / 2);
  for (size_t offset = 2; offset + 1 < size; offset += 2) {
    unsigned code = readUnit(offset);
    if (code >= 0xd800 && code <= 0xdbff) {
      // High surrogate: only a following low surrogate forms a code point.
      // Anything else (end of string, lone high/low surrogate) maps to
      // U+FFFD instead of emitting an invalid UTF-8 sequence.
      if (offset + 3 < size) {
        const unsigned low = readUnit(offset + 2);
        if (low >= 0xdc00 && low <= 0xdfff) {
          code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
          offset += 2;
        } else {
          code = 0xfffd;
        }
      } else {
        code = 0xfffd;
      }
    } else if (code >= 0xdc00 && code <= 0xdfff) {
      code = 0xfffd;
    }
    appendUtf8(out, code);
  }
  return out;
}

void KeyBindModel::snapshot() {
  if (!readsAvailable())
    return;
  for (auto &row : Rows_) {
    if (!row.action || row.action->Header)
      continue;
    scanEngineKeys(row);
    switch (row.state) {
    case BindRowState::kUnavailable:
      // First snapshot: adopt the engine truth as the draft baseline.
      row.draftKeys = row.engineKeys;
      row.state = BindRowState::kClean;
      break;
    case BindRowState::kClean:
      row.draftKeys = row.engineKeys; // untouched rows follow silently
      break;
    case BindRowState::kDirty:
    case BindRowState::kApplyFailed:
      break; // the user's pending edits survive the re-read
    case BindRowState::kQueued:
      if (row.draftKeys == row.engineKeys)
        row.state = BindRowState::kClean; // the write landed
      break;
    }
  }
}

bool KeyBindModel::verify() {
  if (!readsAvailable())
    return false;
  bool changed = false;
  for (auto &row : Rows_) {
    if (row.state != BindRowState::kQueued)
      continue;
    scanEngineKeys(row);
    if (row.draftKeys == row.engineKeys)
      row.state = BindRowState::kClean;
    else
      row.state = BindRowState::kApplyFailed; // reported, retryable
    changed = true;
  }
  return changed;
}

bool KeyBindModel::hasQueued() const {
  for (const auto &row : Rows_)
    if (row.state == BindRowState::kQueued)
      return true;
  return false;
}

bool KeyBindModel::beginCapture(int actionIndex) {
  if (Capturing || ConflictPending)
    return false;
  const RowRuntime *row = runtime(actionIndex);
  if (!row || !row->action || row->action->Header ||
      row->state == BindRowState::kUnavailable)
    return false;
  Capturing = true;
  CaptureRow = actionIndex;
  return true;
}

KeyBindModel::CaptureResult KeyBindModel::captureKey(int keynum) {
  if (!Capturing)
    return CaptureResult::kNotCapturing;
  Capturing = false; // one physical edge per session, whatever it is
  RowRuntime *row = runtime(CaptureRow);
  if (keynum == kEscapeKeynum || !row || !readsAvailable())
    return CaptureResult::kRejected;
  const std::string name = keyNameFor(Seams_, keynum);
  if (!keyNameBindable(name))
    return CaptureResult::kRejected;

  if (hasKey(row->draftKeys, keynum))
    return CaptureResult::kDuplicate; // already this action (draft or truth)

  // Conflict: the key currently drives something else (a listed action or
  // an unlisted user command) - both need an explicit confirmation before
  // the draft changes (task step 4).
  const char *current =
      Seams_.bindingForKey ? Seams_.bindingForKey(keynum) : "";
  if (current && *current && row->action->Command != current) {
    ConflictPending = true;
    ConflictKeynum = keynum;
    ConflictOtherRow = -1;
    ConflictOtherCommand = current;
    for (size_t i = 0; i < Rows_.size(); ++i) {
      if (Rows_[i].action && Rows_[i].action->Command == current) {
        ConflictOtherRow = static_cast<int>(i);
        ConflictOtherCommand.clear();
        break;
      }
    }
    return CaptureResult::kConflict;
  }

  insertKey(row->draftKeys, keynum);
  if (row->state == BindRowState::kClean ||
      row->state == BindRowState::kQueued ||
      row->state == BindRowState::kApplyFailed)
    row->state = BindRowState::kDirty;
  return CaptureResult::kAccepted;
}

void KeyBindModel::cancelCapture() {
  Capturing = false;
  CaptureRow = -1;
}

bool KeyBindModel::resolveConflict(bool accept) {
  if (!ConflictPending)
    return false;
  if (accept) {
    RowRuntime *target = runtime(CaptureRow);
    if (target && !hasKey(target->draftKeys, ConflictKeynum))
      insertKey(target->draftKeys, ConflictKeynum);
    if (target && (target->state == BindRowState::kClean ||
                   target->state == BindRowState::kQueued ||
                   target->state == BindRowState::kApplyFailed))
      target->state = BindRowState::kDirty;
    // The losing side drops the key from its draft only (the engine
    // write is the ONE bind command Apply issues for this key).
    RowRuntime *other = runtime(ConflictOtherRow);
    if (other) {
      const auto itor = findKey(other->draftKeys, ConflictKeynum);
      if (itor != other->draftKeys.end()) {
        other->draftKeys.erase(itor);
        other->state = BindRowState::kDirty;
      }
    }
  }
  ConflictPending = false;
  ConflictKeynum = -1;
  ConflictOtherRow = -1;
  ConflictOtherCommand.clear();
  return true;
}

std::string KeyBindModel::conflictKeyName() const {
  return keyNameFor(Seams_, ConflictKeynum);
}

std::string KeyBindModel::conflictOtherLabel() const {
  if (ConflictOtherRow >= 0 &&
      ConflictOtherRow < static_cast<int>(Actions_.size()))
    return Actions_[ConflictOtherRow].Label;
  return ConflictOtherCommand;
}

bool KeyBindModel::clearActionDraft(int actionIndex) {
  RowRuntime *row = runtime(actionIndex);
  if (!row || !row->action || row->action->Header ||
      row->state == BindRowState::kUnavailable)
    return false;
  if (row->draftKeys.empty())
    return false;
  row->draftKeys.clear();
  row->state = BindRowState::kDirty;
  return true;
}

bool KeyBindModel::removeKeyFromAction(int actionIndex,
                                       const std::string &keyName) {
  RowRuntime *row = runtime(actionIndex);
  if (!row || !row->action || row->action->Header ||
      row->state == BindRowState::kUnavailable)
    return false;
  if (!keyNameBindable(keyName) || !Seams_.keyStringToKeyNum)
    return false;
  const int keynum = Seams_.keyStringToKeyNum(keyName.c_str());
  if (keynum < 0)
    return false;
  const auto itor = findKey(row->draftKeys, keynum);
  if (itor == row->draftKeys.end())
    return false;
  row->draftKeys.erase(itor);
  row->state = BindRowState::kDirty;
  return true;
}

void KeyBindModel::resetDefaults() {
  for (const auto &entry : Defaults_) {
    // Only commands the action list carries: defaults never touch
    // bindings outside this page (escape, toggleconsole, user custom).
    int target = -1;
    for (size_t i = 0; i < Actions_.size(); ++i) {
      if (Actions_[i].Command == entry.second) {
        target = static_cast<int>(i);
        break;
      }
    }
    if (target < 0 || !Seams_.keyStringToKeyNum)
      continue;
    const int keynum = Seams_.keyStringToKeyNum(entry.first.c_str());
    if (keynum < 0 || keynum == kEscapeKeynum)
      continue;
    RowRuntime *row = runtime(target);
    if (!row || row->state == BindRowState::kUnavailable ||
        hasKey(row->draftKeys, keynum))
      continue;
    // One key drives one action: take it out of every other draft.
    for (auto &other : Rows_) {
      const auto itor = findKey(other.draftKeys, keynum);
      if (itor != other.draftKeys.end()) {
        other.draftKeys.erase(itor);
        other.state = BindRowState::kDirty;
      }
    }
    insertKey(row->draftKeys, keynum);
    if (row->state != BindRowState::kDirty)
      row->state = BindRowState::kDirty;
  }
}

void KeyBindModel::cancel() {
  for (auto &row : Rows_) {
    if (row.state == BindRowState::kUnavailable)
      continue;
    row.draftKeys = row.engineKeys;
    row.state = BindRowState::kClean;
  }
  cancelCapture();
  ConflictPending = false;
  ConflictKeynum = -1;
  ConflictOtherRow = -1;
  ConflictOtherCommand.clear();
}

std::map<int, std::string> KeyBindModel::desiredKeys() const {
  std::map<int, std::string> desired;
  for (const auto &row : Rows_) {
    if (!row.action || row.action->Header)
      continue;
    for (int keynum : row.draftKeys)
      desired[keynum] = row.action->Command;
  }
  return desired;
}

std::vector<std::string> KeyBindModel::pendingCommands() const {
  std::vector<std::string> commands;
  // Desired state per key: at most one action's draft claims a key.
  const auto desired = desiredKeys();
  // Bind keys that moved into an action (one command per key - a key
  // moving between actions is exactly one bind, never bind+unbind).
  for (const auto &entry : desired) {
    const char *current =
        Seams_.bindingForKey ? Seams_.bindingForKey(entry.first) : "";
    if (current && entry.second == current)
      continue; // already the truth
    const std::string name = keyNameFor(Seams_, entry.first);
    if (name.empty() || !keyNameBindable(name) ||
        !commandBindable(entry.second) || entry.second.size() > kMaxBindCommand)
      continue; // refused at apply(), not silently skipped here
    commands.push_back("bind \"" + name + "\" \"" + entry.second + "\"");
  }
  // Unbind keys that no draft claims anymore.
  for (const auto &row : Rows_) {
    for (int keynum : row.engineKeys) {
      if (desired.count(keynum) != 0)
        continue;
      const std::string name = keyNameFor(Seams_, keynum);
      if (name.empty() || !keyNameBindable(name))
        continue;
      commands.push_back("unbind \"" + name + "\"");
    }
  }
  return commands;
}

std::vector<BindApplyResult> KeyBindModel::apply() {
  std::vector<BindApplyResult> results;
  if (!readsAvailable() || !Seams_.clientCmd)
    return results;

  // Per-key desired state (the invariant: one key, at most one draft).
  const auto desired = desiredKeys();

  // Row attribution: every write for a key touches the row(s) that list
  // the key in their draft or engine set.
  auto touchedRows = [&](int keynum) {
    std::vector<int> rows;
    for (size_t i = 0; i < Rows_.size(); ++i) {
      if (!Rows_[i].action || Rows_[i].action->Header)
        continue;
      if (hasKey(Rows_[i].draftKeys, keynum) ||
          hasKey(Rows_[i].engineKeys, keynum))
        rows.push_back(static_cast<int>(i));
    }
    return rows;
  };
  std::set<int> refusedRows, queuedRows;
  auto report = [&](int rowIndex, BindApplyOutcome outcome) {
    if (outcome == BindApplyOutcome::kWriteRefused ||
        outcome == BindApplyOutcome::kInvalidRefused)
      refusedRows.insert(rowIndex);
    else if (outcome == BindApplyOutcome::kQueued)
      queuedRows.insert(rowIndex);
    for (auto &result : results) {
      if (result.actionIndex == rowIndex) {
        // Worst outcome wins for multi-write rows.
        if (outcome == BindApplyOutcome::kWriteRefused ||
            outcome == BindApplyOutcome::kInvalidRefused ||
            (outcome == BindApplyOutcome::kQueued &&
             result.outcome == BindApplyOutcome::kApplied))
          result.outcome = outcome;
        return;
      }
    }
    results.push_back({rowIndex, outcome});
  };

  bool anyQueued = false;
  // Binds: keys claimed by a draft whose truth differs.
  for (const auto &entry : desired) {
    const char *current =
        Seams_.bindingForKey ? Seams_.bindingForKey(entry.first) : "";
    if (current && entry.second == current)
      continue;
    const std::string name = keyNameFor(Seams_, entry.first);
    const auto rows = touchedRows(entry.first);
    if (name.empty() || !keyNameBindable(name) ||
        !commandBindable(entry.second) ||
        entry.second.size() > kMaxBindCommand) {
      for (int rowIndex : rows)
        report(rowIndex, BindApplyOutcome::kInvalidRefused);
      continue;
    }
    if (!issueCommand("bind \"" + name + "\" \"" + entry.second + "\"")) {
      for (int rowIndex : rows)
        report(rowIndex, BindApplyOutcome::kWriteRefused);
      continue;
    }
    anyQueued = true;
    for (int rowIndex : rows)
      report(rowIndex, BindApplyOutcome::kQueued);
  }
  // Unbinds: engine keys no draft claims anymore.
  for (auto &row : Rows_) {
    if (!row.action || row.action->Header)
      continue;
    const int index = rowIndex(&row);
    for (int keynum : row.engineKeys) {
      if (desired.count(keynum) != 0)
        continue;
      const std::string name = keyNameFor(Seams_, keynum);
      if (name.empty() || !keyNameBindable(name))
        continue;
      if (!issueCommand("unbind \"" + name + "\"")) {
        report(index, BindApplyOutcome::kWriteRefused);
        continue;
      }
      anyQueued = true;
      report(index, BindApplyOutcome::kQueued);
    }
  }

  // Row states: any refused write this round marks the row failed; rows
  // with issued writes queue for the verification pass below (a previous
  // round's kApplyFailed recovers here when the retry went through).
  for (int index : refusedRows)
    if (RowRuntime *row = runtime(index))
      row->state = BindRowState::kApplyFailed;
  for (int index : queuedRows) {
    if (refusedRows.count(index) != 0)
      continue; // a refused write this round outranks a queued one
    if (RowRuntime *row = runtime(index))
      row->state = BindRowState::kQueued; // recovers a stale kApplyFailed
  }

  // Immediate-confirmation pass: an engine that flushed the buffer inside
  // clientCmd already reports the new truth - those rows turn clean and
  // their results report kApplied; the rest stay kQueued for verify().
  if (anyQueued) {
    for (auto &row : Rows_) {
      if (row.state != BindRowState::kQueued)
        continue;
      scanEngineKeys(row);
      if (row.draftKeys != row.engineKeys)
        continue;
      row.state = BindRowState::kClean;
      const int index = rowIndex(&row);
      for (auto &result : results)
        if (result.actionIndex == index)
          result.outcome = BindApplyOutcome::kApplied;
    }
  }
  return results;
}

std::vector<BindRowView> KeyBindModel::rows() const {
  std::vector<BindRowView> views;
  views.reserve(Rows_.size());
  for (size_t i = 0; i < Rows_.size(); ++i) {
    const auto &row = Rows_[i];
    BindRowView view;
    view.action = row.action;
    view.actionIndex = static_cast<int>(i);
    view.header = row.action && row.action->Header;
    view.state = row.state;
    for (int keynum : row.draftKeys) {
      std::string name = keyNameFor(Seams_, keynum);
      if (!name.empty())
        view.draftKeyNames.push_back(std::move(name));
    }
    views.push_back(std::move(view));
  }
  return views;
}

std::optional<BindRowView> KeyBindModel::find(int actionIndex) const {
  if (actionIndex < 0 || actionIndex >= static_cast<int>(Rows_.size()))
    return std::nullopt;
  const auto &row = Rows_[actionIndex];
  BindRowView view;
  view.action = row.action;
  view.actionIndex = actionIndex;
  view.header = row.action && row.action->Header;
  view.state = row.state;
  for (int keynum : row.draftKeys) {
    std::string name = keyNameFor(Seams_, keynum);
    if (!name.empty())
      view.draftKeyNames.push_back(std::move(name));
  }
  return view;
}

bool KeyBindModel::isDirty() const {
  for (const auto &row : Rows_) {
    if (row.state == BindRowState::kDirty ||
        row.state == BindRowState::kQueued ||
        row.state == BindRowState::kApplyFailed)
      return true;
  }
  return false;
}

KeyBindModel::RowRuntime *KeyBindModel::runtime(int actionIndex) {
  if (actionIndex < 0 || actionIndex >= static_cast<int>(Rows_.size()))
    return nullptr;
  return &Rows_[actionIndex];
}

const KeyBindModel::RowRuntime *KeyBindModel::runtime(int actionIndex) const {
  if (actionIndex < 0 || actionIndex >= static_cast<int>(Rows_.size()))
    return nullptr;
  return &Rows_[actionIndex];
}

// The engine's own table walk (Key_WriteBindings shape): every keynum whose
// binding equals this action's command belongs to it.
void KeyBindModel::scanEngineKeys(RowRuntime &row) const {
  row.engineKeys.clear();
  if (!row.action || row.action->Header || !Seams_.bindingForKey)
    return;
  for (int keynum = 0; keynum < 256; ++keynum) {
    const char *binding = Seams_.bindingForKey(keynum);
    if (binding && row.action->Command == binding)
      row.engineKeys.push_back(keynum);
  }
}

std::string KeyBindModel::keyNameFor(const Seams &seams, int keynum) {
  if (!seams.keyNameForKey)
    return {};
  const char *name = seams.keyNameForKey(keynum);
  return name ? std::string(name) : std::string();
}

// The shapes Key_NameForKey produces (hw.dll FUN_01d717b0): one printable
// ASCII character, or a keyname-table entry of [A-Za-z0-9_]+. Anything else
// would break the bind command quoting - refused, never escaped loosely.
bool KeyBindModel::keyNameBindable(const std::string &name) {
  if (name.empty() || name.size() > 31)
    return false;
  if (name.size() == 1)
    return name[0] >= 0x21 && name[0] <= 0x7e && name[0] != '"';
  for (char c : name) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '_';
    if (!ok)
      return false;
  }
  return true;
}

// kb_act.lst commands feed the quoted command slot; quotes or backslashes
// inside would break out of it (task step 4: no delimiter injection).
bool KeyBindModel::commandBindable(const std::string &command) {
  if (command.empty() || command.size() > kMaxBindCommand)
    return false;
  for (char c : command) {
    if (c == '"' || c == '\\')
      return false;
  }
  return true;
}

bool KeyBindModel::issueCommand(const std::string &command) const {
  return Seams_.clientCmd && Seams_.clientCmd(command.c_str());
}

int KeyBindModel::rowIndex(const RowRuntime *row) const {
  return row && row >= Rows_.data() ? static_cast<int>(row - Rows_.data()) : -1;
}

} // namespace settings
} // namespace services
