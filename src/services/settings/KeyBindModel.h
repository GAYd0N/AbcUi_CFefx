#pragma once

// services::settings - key binding description and transaction model (T11).
//
// Data sources (T01 settings inventory, Keyboard section):
//   * action list  - "gfx/shell/kb_act.lst": quoted "command" "label" pairs;
//     "blank" rows are section headers. The engine file is the ONLY action
//     source - bindings whose command is not listed stay untouched (never a
//     full-table rebuild: Apply writes only the keys the user changed).
//   * defaults     - "gfx/shell/kb_def.lst": quoted "key" "command" pairs,
//     applied in file order (a repeated key takes the later entry, the same
//     order the engine replays them in).
//   * token labels - "resource/valve_english.txt" (quoted pairs) resolve the
//     "#Valve_*" action labels; vanilla parity (SC ships no localized copy),
//     raw token fallback.
//
// Backend seams (binary evidence, hw.dll):
//   * reads   - IGameUIFuncs::Key_BindingForKey (vtable slot 3, hw.dll
//     0x01dbd2b0 -> FUN_01d71cd0) returns the binding of the SAME key table
//     (0x08447988) the `bind` command writes.
//   * names   - IGameUIFuncs::Key_NameForKey / Key_KeyStringToKeyNum (slots
//     2/9; Key_NameForKey is the live-proven vote-panel path).
//   * writes  - verbatim `bind "key" "command"` / `unbind "key"` via
//     pfnClientCmd (Key_Init registration hw.dll 0x01d70a10, handler
//     0x01d71ae0: key -> keynum, argv[2..] space-joined, 1KB limit, K_ESCAPE
//     refused). No second command chain, no Key_SetBinding guesses.
//   * persist - config.cfg is written by Host_Shutdown only (hw.dll
//     0x01dbc990 -> FUN_01d65c70: "unbindall" + Key_WriteBindings + cvars) -
//     the same path T10's archive cvars ride. Apply takes effect in the key
//     table when the command buffer flushes, so Apply verifies immediately
//     and leaves kQueued rows for the next verify() when the truth has not
//     landed yet.
//
// Injection safety (task step 4): keynames come from the engine (single
// printable ASCII char or [A-Za-z0-9_]+), commands come from kb_act.lst and
// may not contain quotes/backslashes. Anything else is refused before the
// clientCmd seam - page text fields never reach a command string.
//
// No engine/Rml includes: offline tests link this exact file with a fake
// binding table (05 doc evidence tier 1/2).

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace services {
namespace settings {

// One kb_act.lst entry. Command is the engine-side command string used
// verbatim in bind writes; Label is the raw resource label ("#Valve_X" or a
// literal). Headers carry no command.
struct BindAction {
  std::string Command;
  std::string Label;
  bool Header = false;
};

// Per-action lifecycle. kClean -> (capture/assign/remove) -> kDirty ->
// (apply) -> kClean, with kQueued covering the buffered-command window and
// kApplyFailed the refused/unconfirmed write. Without binding reads the
// whole section is fail-closed display only.
enum class BindRowState {
  kClean,
  kDirty,       // pending key additions/removals (not yet applied)
  kQueued,      // apply issued the writes; engine truth not confirmed yet
  kApplyFailed, // a write for this row was refused or never took effect
  kUnavailable, // binding reads unavailable, or no snapshot yet
};

enum class BindApplyOutcome {
  kApplied,        // issued and confirmed by an immediate re-read
  kQueued,         // issued; verify() on a later tick confirms or fails it
  kWriteRefused,   // the clientCmd seam refused the (validated) command
  kInvalidRefused, // never issued: keyname/command failed validation
};

struct BindApplyResult {
  int actionIndex = -1;
  BindApplyOutcome outcome = BindApplyOutcome::kApplied;
};

// Read-only projection of one action row for a page render pass.
struct BindRowView {
  const BindAction *action = nullptr;
  int actionIndex = -1;
  std::vector<std::string> draftKeyNames; // keynames in draft order
  BindRowState state = BindRowState::kUnavailable;
  bool header = false;
};

class KeyBindModel {
public:
  // Backend seams - production wiring injects the vgui2ext/abc thunks,
  // tests an in-memory binding table. Function pointers, no captures.
  struct Seams {
    // IGameUIFuncs::Key_BindingForKey: current binding or "" / null.
    const char *(*bindingForKey)(int keynum) = nullptr;
    // IGameUIFuncs::Key_NameForKey: display name or "" / null.
    const char *(*keyNameForKey)(int keynum) = nullptr;
    // IGameUIFuncs::Key_KeyStringToKeyNum: -1 for unknown.
    int (*keyStringToKeyNum)(const char *name) = nullptr;
    // Verbatim engine command (pfnClientCmd); false = refused.
    bool (*clientCmd)(const char *command) = nullptr;
  };

  void setSeams(const Seams &seams) {
    Seams_ = seams;
  }
  bool readsAvailable() const {
    return Seams_.bindingForKey != nullptr && Seams_.keyNameForKey != nullptr;
  }

  // Parsers for the quoted `"a" "b"` pair files. loadActions keeps the
  // "blank" rows as Header entries; loadDefaults replays later duplicates
  // over earlier ones (engine order). Both return success.
  bool loadActions(const char *content);
  bool loadDefaults(const char *content);
  // Token label table ("Valve_X" -> "Move forward") for #Valve_* labels.
  bool loadLabels(const char *content);
  // Token lookup ("Valve_X") for label resolution; false when unknown.
  bool lookupLabel(const std::string &token, std::string &out) const;

  // Valve resource text blobs: kb_act.lst/kb_def.lst are ASCII, but
  // resource/valve_english.txt ships UTF-16LE with a BOM (verified on the
  // shipped file) - a plain byte scan would read NUL-interleaved tokens.
  // Decodes UTF-16LE/BE (BOM-detected) into UTF-8; BOM-less input passes
  // through byte-for-byte. Size-bounded (the source carries NULs).
  static std::string decodeResourceText(const char *data, size_t size);

  int actionCount() const {
    return static_cast<int>(Actions_.size());
  }
  // Rows the page iterates (the action list, headers included).
  int rowCount() const {
    return static_cast<int>(Rows_.size());
  }

  // Re-reads engine truth for every action (keynum 0..255 scan). Drafts
  // are kept for rows the user already touched (the T10 reopen rule);
  // clean rows follow the engine silently. Unlisted commands keep their
  // bindings untouched and invisible. kQueued rows resolve to kClean when
  // the truth landed.
  void snapshot();

  // Capture session (task step 3): exactly one physical edge per session,
  // ESC cancels, the module owns the router token and drives these.
  bool beginCapture(int actionIndex);
  bool capturing() const {
    return Capturing;
  }
  int captureRow() const {
    return CaptureRow;
  }
  enum class CaptureResult {
    kAccepted,     // no conflict: draft gained the key
    kConflict,     // key bound elsewhere: waiting for resolveConflict
    kDuplicate,    // already bound to this action: no-op
    kRejected,     // ESC/unknown key: session over, no change
    kNotCapturing, // no session
  };
  CaptureResult captureKey(int keynum);
  void cancelCapture();
  // Conflict resolution (task step 4): accept = target draft gains the
  // key and the losing row's draft drops it; decline = nothing changed.
  // Both close the prompt; neither writes the backend.
  bool resolveConflict(bool accept);
  bool conflictPending() const {
    return ConflictPending;
  }
  // Facts for the confirmation layer (valid while conflictPending()).
  std::string conflictKeyName() const;
  std::string
  conflictOtherLabel() const; // other action label, or the raw unlisted command

  // Draft edits without capture: remove every key from one action, or one
  // Per-key desired command (one key, at most one draft). Shared by the
  // preview (pendingCommands) and the writer (apply).
  std::map<int, std::string> desiredKeys() const;

  // specific key. Drafts only - the writes wait for apply().
  bool clearActionDraft(int actionIndex);
  bool removeKeyFromAction(int actionIndex, const std::string &keyName);

  // Reset-to-defaults (task step 5): kb_def.lst entries whose command is
  // IN the action list land in the drafts (confirmation is the page's
  // job). Entries pointing at unlisted commands (escape, toggleconsole)
  // are skipped - defaults never touch bindings outside the page.
  void resetDefaults();

  // Discards every draft. Provably writes nothing.
  void cancel();

  // Per-key diff against engine truth, then sequential writes over the
  // clientCmd seam and an immediate re-read: rows whose truth already
  // matches turn clean, the rest stay kQueued for the next verify() (the
  // command buffer flushes between frames). Only changed keys produce
  // traffic; invalid keynames/commands are refused without touching the
  // seam. A key moving between actions is ONE bind command - the write is
  // shared by both affected rows.
  std::vector<BindApplyResult> apply();
  // Re-reads engine truth: kQueued rows resolve to kClean or kApplyFailed.
  // Returns true when any row state changed (the caller re-renders once).
  bool verify();
  // True while kQueued rows exist (apply issued writes whose truth has
  // not been confirmed yet - the module keeps verifying per frame).
  bool hasQueued() const;

  std::vector<BindRowView> rows() const;
  std::optional<BindRowView> find(int actionIndex) const;

  // True while anything would change (or has failed to change): dirty,
  // queued or apply-failed rows exist.
  bool isDirty() const;

  // The exact command strings Apply would issue, in order - pinned by the
  // offline tests (one entry per changed key).
  std::vector<std::string> pendingCommands() const;

private:
  struct RowRuntime {
    const BindAction *action = nullptr;
    std::vector<int> engineKeys; // keynums bound to this command (snapshot)
    std::vector<int> draftKeys;  // draft keynums
    BindRowState state = BindRowState::kUnavailable;
  };

  RowRuntime *runtime(int actionIndex);
  const RowRuntime *runtime(int actionIndex) const;
  void scanEngineKeys(RowRuntime &row) const;
  static std::string keyNameFor(const Seams &seams, int keynum);
  static bool keyNameBindable(const std::string &name);
  static bool commandBindable(const std::string &command);
  bool issueCommand(const std::string &command) const;
  // Used by the unbind loop of apply() (row index while iterating).
  int rowIndex(const RowRuntime *row) const;

  Seams Seams_;
  std::vector<BindAction> Actions_;
  std::vector<RowRuntime> Rows_;
  std::vector<std::pair<std::string, std::string>>
      Defaults_; // keyname -> command, file order
  std::vector<std::pair<std::string, std::string>> Labels_; // token -> text
  bool Capturing = false;
  int CaptureRow = -1;
  bool ConflictPending = false;
  int ConflictKeynum = -1;
  int ConflictOtherRow = -1;
  std::string ConflictOtherCommand; // unlisted-command conflict (no row)
};

} // namespace settings
} // namespace services
