#pragma once

// services::settings - backend-independent setting descriptors and draft
// transactions. The startup JSON repository owns the production table.
//
// Shipped JSON definitions preserve the verified registration data from the
// previous table (frozen in tests/fixtures/LegacySettingsRows.h). Backend
// registration remains independent from the UI definition:
//   * engine cvars carry the registration evidence from the shipped binaries
//     (default string + flags read out of the registering module, UI domain
//     read out of the shipped GameUI page constructors),
//   * user.scr rows carry the shipped advanced-options script (the exact
//     source the original MultiplayerAdvancedDialog parses),
//   * AbcUi cvars cite our own CREATE_CVAR registration (the source IS the
//     default source).
// JSON extensions may describe additional cvars; a row whose cvar is not
// registered at runtime renders as unavailable instead of silently no-oping.
//
// Draft transaction: the page never writes a cvar outside Apply. snapshot()
// reads engine truth; edits land in per-row drafts; apply() writes through
// the injected seams (production: CVAR_SET_FLOAT / CVAR_SET_STRING - engine
// APIs, never a concatenated "name value" command) and reads back; cancel()
// discards drafts and provably writes nothing.
//
// The ONLY command-channel writes are the kCommandToggle rows (in_mlook /
// in_jlook: +mlook / -mlook - the vanilla page sends the same commands and
// reads IsKeyDown; there is no cvar behind them). Their effect lands on the
// engine command flush, so apply leaves them kQueued and snapshot() settles
// them against the button state within a couple of frames (the T11 verify
// beat, one model earlier).
//
// String rows (name / model / cl_logofile / cl_logocolor) validate BEFORE
// the write: printable bytes only, no quote/backslash/control characters,
// enum options must come from the scanned (or fixed) option set, and name
// is capped at the RES maxchars=63 byte budget. The engine then receives a
// CVAR_SET_STRING, not a command - nothing user-typed is ever spliced into
// a command line (T13 boundary).
//
// External-change rule (V10): snapshot() refreshes rows the user has not
// touched and flags rows it HAS touched as kConflict instead of quietly
// overwriting the draft. Apply is a sequence of independent per-cvar writes -
// the engine exposes no cross-cvar atomic transaction, so a partial failure
// keeps the failed rows dirty with per-row outcomes and never pretends the
// batch committed.
//
// No engine/Rml includes: the exact production model links into offline
// tests with a fake backend (05 doc evidence tier 1/2). Persistence stays
// engine-side (config.cfg for archive cvars; session-only for the ones the
// engine registers without FCVAR_ARCHIVE - the original behaves the same).

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace services {
namespace settings {

enum class SettingKind {
  kBool,          // domain {0, 1}; toggle control; cvar float write
  kFloat,         // [Min, Max] numeric entry; cvar float write
  kEnum,          // LIST from the shipped table: value picked from Options
  kNegate,        // m_pitch: bool view over the sign; toggle flips the draft
  kText,          // name: validated text row; CVAR_SET_STRING write
  kStringEnum,    // model / cl_logofile / cl_logocolor: token from a fixed
                  // or scanned option set; CVAR_SET_STRING write
  kCommandToggle, // in_mlook/in_jlook: read IsKeyDown(state), write the
                  // +/- command; kQueued until the engine flushes
};

// One LIST option (user.scr LIST rows and cl_logocolor). Label is a lang
// token ("#Valve_Green") or a literal; the page resolves and escapes it.
struct SettingEnumOption {
  float Value;
  const char *Label;
};

// One string-enum choice with its own display label: the value is what the
// cvar receives (a scanned file's path), the label is what the dropdown
// shows (the file name without its extension). Rows backed by a scan install
// these through setStringChoices; rows whose label equals their value keep
// using setStringOptions.
struct StringChoice {
  std::string Value;
  std::string Label;
};

// Borrowed description view. The JSON repository (or UserScrTable for direct
// script consumers) owns all strings and options and must outlive the model.
struct SettingRow {
  const char *Cvar; // engine-registered name, referenced verbatim
  SettingKind Kind;
  float Min;              // UI domain (kBool/kCommandToggle: 0)
  float Max;              // UI domain (kBool/kCommandToggle: 1)
  float Step;             // slider stride hint (0 = free numeric entry)
  float DefaultValue;     // verified registration / user.scr default
  const char *LabelToken; // "#Settings_*" lang token
  const char *Section;    // "#Settings_Sec*" group header token; the page
                          // renders a divider whenever it changes
  const SettingEnumOption *Options = nullptr; // kEnum option table
  int OptionCount = 0;
  const char *StateName = nullptr;     // kCommandToggle: IsKeyDown state name
                                       // ("in_mlook"); kStringEnum: fixed
                                       // option set name (diagnostics only)
  const char *DefaultString = nullptr; // kText/kStringEnum default value
                                       // (null: format DefaultValue)
  const char *OnCommand = nullptr;     // kCommandToggle: checked-side command
  const char *OffCommand = nullptr;    // kCommandToggle: unchecked-side command
  bool Bounded = true;
  bool ReadOnly = false;
  const char *RequireCvar = nullptr;
  bool RequireEnabled = false;
  bool HasDefault = true;
  // The row's value is a player-palette HUE (the engine's topcolor /
  // bottomcolor): the page paints the slider thumb with the colour of the
  // current value (framework::ColorUtil::playerHueColor). Only a bounded
  // float row may declare it (the schema enforces that).
  bool HueSwatch = false;
  // The scanned options are FILE paths (menu video / music): the page labels
  // them with the file stem, and the write validation accepts a relative
  // path (no traversal) instead of the bare enum-token charset.
  bool FileChoices = false;
};

// Per-row lifecycle. kClean -> (edit) -> kDirty -> (apply) -> kClean, with
// the error branches never silently losing the user's input. kMissing is the
// fail-closed state for a cvar the engine never registered. kQueued is the
// kCommandToggle post-apply state: the command is in the engine buffer, the
// state read-back has not caught up yet.
enum class RowState {
  kClean,       // draft == engine snapshot; nothing to apply
  kDirty,       // user edit pending (not yet applied)
  kInvalid,     // last committed draft text is not a value in the domain
  kConflict,    // pending edit + the engine value changed behind the page
  kQueued,      // command accepted, engine buffer not flushed yet
  kApplyFailed, // apply wrote nothing effective for this row; draft kept
  kMissing,     // cvar not registered: row unavailable, never editable
};

enum class ApplyOutcome {
  kApplied,
  kQueued,           // command channel write; settles via snapshot()
  kWriteFailed,      // backend refused the write
  kReadbackMismatch, // write accepted, engine does not report the value
  kInvalidRefused,   // not written: the draft text never parsed into domain
};

struct ApplyResult {
  std::string cvar;
  ApplyOutcome outcome = ApplyOutcome::kApplied;
};

// Read-only projection of one row for a page render pass.
struct RowView {
  const SettingRow *row = nullptr;
  float engineValue = 0.0f;
  std::string engineText;  // kText/kStringEnum: the engine's stored string
  std::string draftText;   // committed field text (valid or not)
  float draftValue = 0.0f; // meaningful when state != kInvalid
  RowState state = RowState::kClean;
  bool missing = false; // cvar not registered: row unavailable
  const std::vector<std::string> *options = nullptr; // kStringEnum scan
  // Parallel to options: the dropdown label per option (file rows show the
  // stem). Entries may be empty - the page then falls back to its own rules.
  const std::vector<std::string> *optionLabels = nullptr;
};

class SettingsModel {
public:
  // Backend seams - the production wiring injects the abc::CvarCmd
  // wrappers; tests inject an in-memory fake. Function pointers, no
  // captures (EngineUiBackend's Seams shape).
  struct Seams {
    bool (*exists)(const char *name) = nullptr;
    float (*readFloat)(const char *name) = nullptr;
    // Must be a real cvar write (engine-side callbacks and persistence
    // ride along); returns false when the write did not happen.
    bool (*writeFloat)(const char *name, float value) = nullptr;
    // T13 string rows: real engine cvar string write + read (never a
    // command); readString returns null when the cvar is missing.
    bool (*writeString)(const char *name, const char *value) = nullptr;
    const char *(*readString)(const char *name) = nullptr;
    // T13 kCommandToggle: IGameUIFuncs::IsKeyDown and the engine command
    // channel (buffered - the model treats the effect as deferred).
    bool (*isKeyDown)(const char *stateName) = nullptr;
    bool (*clientCmd)(const char *command) = nullptr;
  };

  SettingsModel(const SettingRow *rows, int count);
  SettingsModel() = default;

  // Replaces the row table (module init: JSON + user.scr overrides).
  // Resets all runtime state - call
  // before the first snapshot.
  void setTable(const SettingRow *rows, int count);

  void setSeams(const Seams &seams) {
    Seams_ = seams;
  }

  // kStringEnum rows: installs the scanned option set (model/logo lists
  // from the module's directory scan). Values already equal to the engine
  // value stay acceptable even when not in the list (a downloaded model
  // must not be force-changed by opening the page).
  void setStringOptions(const char *cvar, std::vector<std::string> options);
  // The labelled form (file scans): values are stored, labels are shown.
  // Both vectors are sorted together by value.
  void setStringChoices(const char *cvar, std::vector<StringChoice> choices);

  // Re-reads engine truth. Untouched rows adopt the new value silently;
  // rows with a pending user edit keep the draft and flip to kConflict
  // (the page prompts - it never quietly overwrites). Missing rows stay
  // missing: an unregistered cvar is reported, not guessed. kQueued rows
  // resolve here: the flushed command state matches -> kClean, still not
  // after kQueuedTicksLimit snapshots -> kApplyFailed.
  void snapshot();

  // Commits one field edit. Parses the full text as a finite float and
  // validates the domain: on success the row turns kDirty (kConflict if
  // the engine moved meanwhile - snapshot() flags first), on failure the
  // text is kept for display and the row turns kInvalid. Returns success.
  bool setDraftText(const char *cvar, const char *text);
  // Toggle path for kBool rows (always in domain); also drives kNegate
  // (sign flip over the current draft magnitude) and kCommandToggle
  // (draft target state; command written on apply).
  bool setDraftBool(const char *cvar, bool value);
  // kEnum rows: value must be one of the row's Options.
  bool setDraftEnum(const char *cvar, float value);
  // kText / kStringEnum rows: validated text (printable, no quote or
  // control bytes, length cap; enum options must be known). Returns false
  // and keeps the text for display (kInvalid) on rejection.
  bool setDraftString(const char *cvar, const char *text);

  // Conflict resolution: drop the pending edit and take the engine value.
  bool adoptEngineValue(const char *cvar);

  // Drafts := verified table defaults (rows whose engine value already
  // equals the default turn clean). Confirmation is the page's job; the
  // write still waits for apply().
  void resetDefaults();
  void resetDefaults(const std::vector<std::string> &Cvars);

  // Discards every draft and error flag. Provably writes nothing.
  void cancel();

  // Sequential per-row write + read-back over the seams. Clean and missing
  // rows are skipped; invalid drafts are refused without touching the
  // backend. Partial failure keeps the failed rows at kApplyFailed (still
  // dirty); applied rows re-snapshot to the written value. kCommandToggle
  // rows land kQueued (command channel, settles on the frame beat).
  std::vector<ApplyResult> apply();

  std::vector<RowView> rows() const;
  std::optional<RowView> find(const char *cvar) const;

  // True while anything would change (or failed to change) on Apply:
  // dirty, invalid, conflict, queued or apply-failed rows exist.
  bool isDirty() const;
  bool hasConflicts() const;
  // True while a kCommandToggle write is still settling (the module's
  // per-frame beat re-snapshots until it resolves).
  bool hasQueued() const;
  bool editable(const char *Cvar) const;

  // user.scr parser (the original CInfoDescription grammar): fills the
  // owning table with one row per DESCRIPTION entry (BOOL / NUMBER min max
  // / STRING / LIST "label" "value" ...). Malformed entries are skipped;
  // a table with zero rows reports false (caller keeps the static
  // JSON fallback). The arena strings outlive any borrowed row views.
  struct UserScrTable {
    std::vector<SettingRow> Rows;
    std::vector<std::vector<SettingEnumOption>> Options;
    // Arena for the cvar/prompt/default strings the rows point into. A
    // deque, NOT a vector: push_back never moves existing elements, so
    // the pointers already stored in Rows cannot dangle (vector
    // reallocation moves SSO string objects and the stored c_str()
    // pointers with them - caught by the multi-entry parser test).
    std::deque<std::string> Strings;
  };
  static bool parseUserScr(const char *text, UserScrTable &out);

private:
  struct RowRuntime {
    const SettingRow *row = nullptr;
    float engineValue = 0.0f;
    std::string engineText;
    std::string draftText;
    float draftValue = 0.0f;
    std::vector<std::string> options;      // kStringEnum scan options
    std::vector<std::string> optionLabels; // parallel display labels
    int queuedTicks = 0;
    // Pre-snapshot placeholder: the first snapshot() runs the kClean
    // adoption and lands every registered row on the engine truth.
    RowState state = RowState::kClean;
    bool missing = true;
  };

  RowRuntime *runtime(const char *cvar);
  void resetDefaultsImpl(const std::vector<std::string> *Cvars);
  const RowRuntime *runtime(const char *cvar) const;
  void adoptEngineLocked(RowRuntime &target, float value);
  void adoptEngineTextLocked(RowRuntime &target, const std::string &text);
  static std::string formatValue(float value);
  static bool parseValue(const char *text, float &out);
  // kText/kStringEnum write validation (T13 boundary): printable bytes
  // only, no quote/backslash/control characters, kText capped at 63 bytes
  // (the original RES maxchars), enum tokens limited to the option set.
  bool validateString(const RowRuntime &entry, const char *text) const;
  float engineTruthLocked(const RowRuntime &entry) const;

  Seams Seams_;
  std::vector<RowRuntime> Rows_;
};

// kText byte budget: the shipped OptionsSubMultiplayer.res NameEntry
// maxchars=63 (the original page caps at the same count; the engine
// PRINTABLEONLY flag handles the rest).
constexpr size_t kNameMaxBytes = 63;
// kQueued settle budget: the engine command buffer flushes within a frame;
// two snapshot beats without a matching state read mean the command did not
// take (reported as kApplyFailed instead of spinning forever).
constexpr int kQueuedTicksLimit = 2;

// The file-choice text rule (menu video / music): a mod-relative path with
// forward slashes, no traversal, no drive/absolute forms and nothing that
// could split or escape an engine command. The settings model validates file
// rows with it and the menu music validates the cvar's track text with the
// same rule before it becomes part of an "mp3 loop <track>" command.
bool fileChoiceValueValid(const char *text);

} // namespace settings
} // namespace services
