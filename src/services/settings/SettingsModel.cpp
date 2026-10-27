// services::settings implementation - see SettingsModel.h. No engine or Rml
// dependency: every backend touchpoint rides the injected seams, so the
// production model links into the offline acceptance unchanged.
#include "SettingsModel.h"
#include "ChoiceValidation.h"

#include <abc/Parse.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <utility>

namespace services {
namespace settings {

namespace {

// Engine floats round-trip through the cvar storage; the epsilon only guards
// against representation noise, not real changes.
constexpr float kValueEpsilon = 1e-6f;

bool sameValue(float a, float b) {
  return std::fabs(a - b) <= kValueEpsilon;
}

bool rowUsesString(const SettingRow *row) {
  return row->Kind == SettingKind::kText ||
         row->Kind == SettingKind::kStringEnum;
}

// Printable, quote-free, control-free byte check for the string rows. UTF-8
// continuation bytes (>= 0x80) are allowed - player names are localized
// text. This is the seam-side validation the T13 boundary asks for: nothing
// user-typed ever reaches an engine API unvalidated.
bool stringBytesValid(const char *text, bool nameLike) {
  if (!text)
    return false;
  const size_t len = std::strlen(text);
  if (nameLike && (len == 0 || len > kNameMaxBytes))
    return false; // the original page refuses an empty name the same way
  for (const char *p = text; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x20 || c == 0x7f)
      return false;
    if (c == '"' || c == '\\' || c == '`' || c == ';')
      return false;
  }
  return true;
}

} // namespace

// File-choice rows (menu video / music scans): the value is a relative path
// into the plugin's resource tree, so the bare enum-token charset cannot
// apply - but the path stays inside the mod: forward slashes only, no
// traversal, no drive/absolute forms, no quotes or separators that could
// escape an engine command (defence in depth; the set itself comes from our
// own scan). The menu music validates the cvar's track text with the same
// rule before it is spliced into an "mp3 loop <track>" command.
bool fileChoiceValueValid(const char *text) {
  if (!text)
    return false;
  std::string_view value(text);
  if (value.starts_with("mod://"))
    value.remove_prefix(6);
  if (value.empty() || value.front() == '/' || value.back() == '/' ||
      value.find("..") != value.npos ||
      value.find_first_of("\"'`\\|<>*?[]:") != value.npos)
    return false;
  return std::ranges::none_of(
      value, [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

SettingsModel::SettingsModel(const SettingRow *rows, int count) {
  setTable(rows, count);
}

void SettingsModel::setTable(const SettingRow *rows, int count) {
  Rows_.clear();
  Rows_.reserve(count > 0 ? static_cast<size_t>(count) : 0);
  for (int i = 0; i < count; i++) {
    RowRuntime runtime;
    runtime.row = &rows[i];
    Rows_.push_back(std::move(runtime));
  }
}

void SettingsModel::setStringOptions(const char *cvar,
                                     std::vector<std::string> options) {
  std::vector<StringChoice> choices;
  choices.reserve(options.size());
  for (auto &value : options)
    choices.push_back({std::move(value), std::string()});
  setStringChoices(cvar, std::move(choices));
}

void SettingsModel::setStringChoices(const char *cvar,
                                     std::vector<StringChoice> choices) {
  RowRuntime *entry = runtime(cvar);
  if (!entry || entry->row->Kind != SettingKind::kStringEnum)
    return;
  // Sorted by value for the page's select and for binary_search; the labels
  // ride along so the two vectors stay parallel. Values already equal to
  // the engine value stay acceptable even when not in the list
  // (setDraftString keeps that door open).
  std::sort(choices.begin(), choices.end(),
            [](const StringChoice &A, const StringChoice &B) {
              return A.Value < B.Value;
            });
  entry->options.clear();
  entry->optionLabels.clear();
  entry->options.reserve(choices.size());
  entry->optionLabels.reserve(choices.size());
  for (auto &choice : choices) {
    entry->options.push_back(std::move(choice.Value));
    entry->optionLabels.push_back(std::move(choice.Label));
  }
}

void SettingsModel::setChoiceRefresh(
    const char *sourceCvar, std::function<void(SettingsModel &)> refresh) {
  ChoiceSource_ = sourceCvar ? sourceCvar : "";
  ChoiceRefresh_ = std::move(refresh);
}

void SettingsModel::refreshChoices(const char *changedCvar) {
  if (!ChoiceRefresh_ || RefreshingChoices_ ||
      (changedCvar && ChoiceSource_ != changedCvar))
    return;
  RefreshingChoices_ = true;
  ChoiceRefresh_(*this);
  RefreshingChoices_ = false;
}

SettingsModel::RowRuntime *SettingsModel::runtime(const char *cvar) {
  if (!cvar)
    return nullptr;
  for (auto &entry : Rows_) {
    if (std::string_view(entry.row->Cvar) == cvar)
      return &entry;
  }
  return nullptr;
}

const SettingsModel::RowRuntime *
SettingsModel::runtime(const char *cvar) const {
  if (!cvar)
    return nullptr;
  for (const auto &entry : Rows_) {
    if (std::string_view(entry.row->Cvar) == cvar)
      return &entry;
  }
  return nullptr;
}

std::string SettingsModel::formatValue(float value) {
  // %g keeps the table's values readable ("3", "0.7", "20") without
  // decimal trails; %.1f would invent precision the domain does not carry.
  return std::format("{:g}", value);
}

bool SettingsModel::parseValue(const char *text, float &out) {
  if (!text)
    return false;
  // Whole-view parse: "3abc" and "" are input errors, not 3.
  const auto parsed = abc::ParseFloat(text);
  if (!parsed || !std::isfinite(*parsed))
    return false;
  out = *parsed;
  return std::isfinite(out);
}

void SettingsModel::adoptEngineLocked(RowRuntime &target, float value) {
  target.engineValue = value;
  target.draftValue = value;
  target.draftText = formatValue(value);
  target.engineText.clear();
  target.queuedTicks = 0;
  target.state = RowState::kClean;
}

void SettingsModel::adoptEngineTextLocked(RowRuntime &target,
                                          const std::string &text) {
  target.engineText = text;
  target.draftText = text;
  target.draftValue = 0.0f;
  target.queuedTicks = 0;
  target.state = RowState::kClean;
}

// The per-kind engine truth for float-domain rows; string rows carry their
// truth in engineText and report the float side as 0.
float SettingsModel::engineTruthLocked(const RowRuntime &entry) const {
  if (rowUsesString(entry.row))
    return 0.0f;
  if (entry.row->Kind == SettingKind::kCommandToggle)
    return Seams_.isKeyDown && entry.row->StateName &&
                   Seams_.isKeyDown(entry.row->StateName)
               ? 1.0f
               : 0.0f;
  return Seams_.readFloat ? Seams_.readFloat(entry.row->Cvar) : 0.0f;
}

void SettingsModel::snapshot() {
  for (auto &entry : Rows_) {
    // kCommandToggle rows have NO cvar behind them (in_mlook/in_jlook are
    // client button states read through IGameUIFuncs::IsKeyDown): their
    // availability is the seam pair, not the cvar table. Everything else
    // fails closed on an unregistered cvar.
    entry.missing =
        entry.row->Kind == SettingKind::kCommandToggle
            ? (!Seams_.isKeyDown || !entry.row->StateName || !Seams_.clientCmd)
            : (!Seams_.exists || !Seams_.exists(entry.row->Cvar));
    if (entry.row->RequireCvar) {
      entry.missing = entry.missing || !Seams_.exists ||
                      !Seams_.exists(entry.row->RequireCvar) ||
                      (entry.row->RequireEnabled &&
                       (!Seams_.readFloat ||
                        Seams_.readFloat(entry.row->RequireCvar) <= 0));
    }
    if (!entry.missing && entry.state == RowState::kMissing)
      entry.state = RowState::kClean;
    if (entry.missing) {
      entry.state = RowState::kMissing;
      continue;
    }
    const float engine = engineTruthLocked(entry);
    std::string engineString;
    if (rowUsesString(entry.row)) {
      const char *text =
          Seams_.readString ? Seams_.readString(entry.row->Cvar) : nullptr;
      engineString = text ? text : "";
    }
    const bool engineChanged = rowUsesString(entry.row)
                                   ? engineString != entry.engineText
                                   : !sameValue(engine, entry.engineValue);

    if (entry.state == RowState::kQueued) {
      // The command was accepted; the read-back decides. Matching is
      // the settle, silence past the tick budget is the failure (the
      // engine buffer flushes within a frame - two beats is generous).
      const bool landed = rowUsesString(entry.row)
                              ? engineString == entry.draftText
                              : sameValue(engine, entry.draftValue);
      entry.engineValue = engine;
      entry.engineText = engineString;
      if (landed) {
        adoptEngineLocked(entry, engine);
        entry.draftText =
            rowUsesString(entry.row) ? engineString : formatValue(engine);
      } else if (++entry.queuedTicks >= kQueuedTicksLimit) {
        entry.state = RowState::kApplyFailed;
      }
      continue;
    }

    entry.engineValue = engine;
    if (rowUsesString(entry.row))
      entry.engineText = engineString;

    switch (entry.state) {
    case RowState::kDirty:
    case RowState::kConflict:
      // Pending user edit: keep the draft; an outside change surfaces
      // as kConflict, an outside revert downgrades back to plain dirty.
      entry.state = engineChanged ? RowState::kConflict : RowState::kDirty;
      break;
    case RowState::kApplyFailed:
      // The read-back disagreed at apply time; the fresh read decides:
      // the value may have landed after all, an outside change makes the
      // conflict real, otherwise the edit is simply pending again.
      if (rowUsesString(entry.row) ? engineString == entry.draftText
                                   : sameValue(engine, entry.draftValue)) {
        if (rowUsesString(entry.row))
          adoptEngineTextLocked(entry, engineString);
        else
          adoptEngineLocked(entry, engine);
      } else {
        entry.state = engineChanged ? RowState::kConflict : RowState::kDirty;
      }
      break;
    case RowState::kClean:
    case RowState::kInvalid:
      // kClean: the untouched field follows the engine silently.
      // kInvalid: the committed text never parsed - the engine read
      // refreshes, the user still owes a valid value.
      if (entry.state == RowState::kClean) {
        if (rowUsesString(entry.row))
          adoptEngineTextLocked(entry, engineString);
        else
          adoptEngineLocked(entry, engine);
      }
      break;
    case RowState::kQueued:
    case RowState::kMissing:
      break; // handled above
    }
  }
  refreshChoices();
}

bool SettingsModel::setDraftText(const char *cvar, const char *text) {
  RowRuntime *entry = runtime(cvar);
  if (!entry || !editable(cvar) || rowUsesString(entry->row))
    return false;
  float parsed = 0.0f;
  if (!parseValue(text, parsed) ||
      (entry->row->Bounded &&
       (parsed < entry->row->Min || parsed > entry->row->Max))) {
    entry->draftText = text ? text : "";
    entry->state = RowState::kInvalid;
    return false;
  }
  entry->draftText = text;
  entry->draftValue = parsed;
  if (sameValue(parsed, entry->engineValue)) {
    entry->state = RowState::kClean; // typed back to the live value
  } else if (entry->state != RowState::kConflict) {
    entry->state = RowState::kDirty;
  }
  return true;
}

bool SettingsModel::setDraftBool(const char *cvar, bool value) {
  RowRuntime *entry = runtime(cvar);
  if (!entry || !editable(cvar))
    return false;
  const float next = value ? 1.0f : 0.0f;
  if (entry->row->Kind == SettingKind::kBool) {
    entry->draftText = value ? "1" : "0";
    entry->draftValue = next;
  } else if (entry->row->Kind == SettingKind::kCommandToggle) {
    // Draft = the target button state; the command rides apply().
    entry->draftText = value ? "1" : "0";
    entry->draftValue = next;
  } else if (entry->row->Kind == SettingKind::kNegate) {
    // Checked = negative m_pitch (the shipped CCvarNegateCheckButton:
    // checked iff value < 0). The toggle flips the sign of the current
    // magnitude; a zero magnitude stays zero - the model never invents
    // a base value the engine does not carry.
    const float magnitude =
        std::fabs(entry->state == RowState::kClean ? entry->engineValue
                                                   : entry->draftValue);
    entry->draftText = formatValue(value ? -magnitude : magnitude);
    entry->draftValue = value ? -magnitude : magnitude;
  } else {
    return false;
  }
  if (sameValue(entry->draftValue, entry->engineValue)) {
    entry->state = RowState::kClean;
  } else if (entry->state != RowState::kConflict) {
    entry->state = RowState::kDirty;
  }
  return true;
}

bool SettingsModel::setDraftEnum(const char *cvar, float value) {
  RowRuntime *entry = runtime(cvar);
  if (!entry || !editable(cvar) || entry->row->Kind != SettingKind::kEnum)
    return false;
  bool known = false;
  for (int i = 0; i < entry->row->OptionCount; ++i)
    known = known || sameValue(entry->row->Options[i].Value, value);
  if (!known) {
    entry->draftText = formatValue(value);
    entry->state = RowState::kInvalid;
    return false;
  }
  entry->draftText = formatValue(value);
  entry->draftValue = value;
  if (sameValue(value, entry->engineValue)) {
    entry->state = RowState::kClean;
  } else if (entry->state != RowState::kConflict) {
    entry->state = RowState::kDirty;
  }
  return true;
}

bool SettingsModel::validateString(const RowRuntime &entry,
                                   const char *text) const {
  if (!text)
    return false;
  if (entry.row->Kind == SettingKind::kText && Seams_.validateText &&
      !Seams_.validateText(entry.row->Cvar, text))
    return false;
  if (!*text && entry.row->Kind == SettingKind::kText && entry.row->AllowEmpty)
    return true;
  if (!stringBytesValid(text, entry.row->Kind == SettingKind::kText))
    return false;
  if (entry.row->Kind == SettingKind::kStringEnum) {
    // Defence in depth: the token charset is enforced even though the
    // option sets come from our own scans, and the token must be one of
    // the scanned or fixed options - or exactly the live engine value
    // (a downloaded model must not be force-changed by opening the page).
    // File rows carry a relative path instead of a bare token, with the
    // path-shaped charset above.
    if (entry.row->FileChoices ? !fileChoiceValueValid(text)
                               : !choiceTokenValid(text))
      return false;
    bool known = std::binary_search(entry.options.begin(), entry.options.end(),
                                    std::string(text));
    for (int i = 0; !known && i < entry.row->OptionCount; ++i)
      known = std::string_view(entry.row->Options[i].Label) ==
              text; // fixed token table
    if (!known)
      return false;
  }
  return true;
}

bool SettingsModel::setDraftString(const char *cvar, const char *text) {
  RowRuntime *entry = runtime(cvar);
  if (!entry || !editable(cvar) || !rowUsesString(entry->row))
    return false;
  // Committing the live value back is always the clean state - BEFORE the
  // charset gate. The select widgets dispatch a change when their first
  // option lands (the T12 Add() pin), and an empty engine value would
  // otherwise fail the token charset and stick the row at kInvalid. A
  // clean draft never writes, so no unvalidated text can reach the engine
  // through this door.
  if (text && entry->engineText == text) {
    adoptEngineTextLocked(*entry, entry->engineText);
    refreshChoices(cvar);
    return true;
  }
  if (!validateString(*entry, text)) {
    entry->draftText = text ? text : "";
    entry->state = RowState::kInvalid;
    refreshChoices(cvar);
    return false;
  }
  entry->draftText = text;
  if (entry->draftText == entry->engineText) {
    entry->state = RowState::kClean;
  } else if (entry->state != RowState::kConflict) {
    entry->state = RowState::kDirty;
  }
  refreshChoices(cvar);
  return true;
}

bool SettingsModel::adoptEngineValue(const char *cvar) {
  RowRuntime *entry = runtime(cvar);
  if (!entry || !editable(cvar))
    return false;
  if (rowUsesString(entry->row))
    adoptEngineTextLocked(*entry, entry->engineText);
  else
    adoptEngineLocked(*entry, entry->engineValue);
  refreshChoices(cvar);
  return true;
}

void SettingsModel::resetDefaults() {
  resetDefaultsImpl(nullptr);
}

void SettingsModel::resetDefaults(const std::vector<std::string> &Cvars) {
  resetDefaultsImpl(&Cvars);
}

void SettingsModel::resetDefaultsImpl(const std::vector<std::string> *Cvars) {
  for (auto &Entry : Rows_) {
    if (Cvars && std::find(Cvars->begin(), Cvars->end(), Entry.row->Cvar) ==
                     Cvars->end())
      continue;
    if (!editable(Entry.row->Cvar) || !Entry.row->HasDefault)
      continue;
    if (rowUsesString(Entry.row)) {
      const std::string Fallback =
          Entry.row->DefaultString ? Entry.row->DefaultString : std::string();
      if (Fallback == Entry.engineText) {
        adoptEngineTextLocked(Entry, Entry.engineText);
      } else {
        Entry.draftText = Fallback;
        Entry.draftValue = 0.0f;
        Entry.state = RowState::kDirty;
      }
      continue;
    }
    const float Fallback = Entry.row->DefaultValue;
    Entry.draftText = formatValue(Fallback);
    Entry.draftValue = Fallback;
    Entry.state = sameValue(Fallback, Entry.engineValue) ? RowState::kClean
                                                         : RowState::kDirty;
  }
  refreshChoices();
}

void SettingsModel::cancel() {
  for (auto &entry : Rows_) {
    if (entry.missing)
      continue;
    if (rowUsesString(entry.row))
      adoptEngineTextLocked(entry, entry.engineText);
    else
      adoptEngineLocked(entry, entry.engineValue);
  }
  refreshChoices();
}

services::settings::ApplyResult SettingsModel::applyRow(RowRuntime &entry) {
  ApplyResult result{entry.row->Cvar, ApplyOutcome::kApplied};
  if (rowUsesString(entry.row)) {
    if (!Seams_.writeString ||
        !Seams_.writeString(entry.row->Cvar, entry.draftText.c_str())) {
      result.outcome = ApplyOutcome::kWriteFailed;
    } else {
      const char *readback =
          Seams_.readString ? Seams_.readString(entry.row->Cvar) : nullptr;
      if (!readback || entry.draftText != readback)
        result.outcome = ApplyOutcome::kReadbackMismatch;
    }
  } else if (entry.row->Kind == SettingKind::kCommandToggle) {
    const char *command =
        entry.draftValue > 0.5f ? entry.row->OnCommand : entry.row->OffCommand;
    if (!Seams_.clientCmd || !command || !Seams_.clientCmd(command)) {
      result.outcome = ApplyOutcome::kWriteFailed;
    } else {
      result.outcome = ApplyOutcome::kQueued;
    }
  } else {
    if (!Seams_.writeFloat ||
        !Seams_.writeFloat(entry.row->Cvar, entry.draftValue)) {
      result.outcome = ApplyOutcome::kWriteFailed;
    } else {
      const float readback = Seams_.readFloat
                                 ? Seams_.readFloat(entry.row->Cvar)
                                 : entry.draftValue;
      if (!sameValue(readback, entry.draftValue))
        result.outcome = ApplyOutcome::kReadbackMismatch;
    }
  }
  if (result.outcome == ApplyOutcome::kApplied) {
    if (rowUsesString(entry.row))
      adoptEngineTextLocked(entry, entry.draftText);
    else
      adoptEngineLocked(entry, entry.draftValue);
  } else if (result.outcome == ApplyOutcome::kQueued) {
    // Deferred: keep the draft, wait for the flush on the frame beat.
    entry.state = RowState::kQueued;
    entry.queuedTicks = 0;
  } else {
    // The engine state is unknown/mixed here: keep the draft and the
    // dirty flag so the row stays visible and retryable. The next
    // snapshot() decides conflict-vs-dirty from the real engine.
    entry.state = RowState::kApplyFailed;
  }
  return result;
}

std::vector<ApplyResult> SettingsModel::apply() {
  refreshChoices();
  std::vector<ApplyResult> results;
  for (auto &entry : Rows_) {
    if (!editable(entry.row->Cvar) || entry.state == RowState::kClean ||
        entry.state == RowState::kQueued)
      continue;
    if (entry.state == RowState::kInvalid) {
      results.push_back({entry.row->Cvar, ApplyOutcome::kInvalidRefused});
      continue;
    }
    results.push_back(applyRow(entry));
  }
  return results;
}

std::optional<ApplyResult> SettingsModel::applyOne(const char *cvar) {
  refreshChoices();
  RowRuntime *entry = runtime(cvar);
  if (!entry || !editable(cvar) || entry->state == RowState::kClean ||
      entry->state == RowState::kQueued)
    return std::nullopt;
  if (entry->state == RowState::kInvalid)
    return ApplyResult{entry->row->Cvar, ApplyOutcome::kInvalidRefused};
  return applyRow(*entry);
}

std::vector<RowView> SettingsModel::rows() const {
  std::vector<RowView> views;
  views.reserve(Rows_.size());
  for (const auto &entry : Rows_) {
    RowView view;
    view.row = entry.row;
    view.engineValue = entry.engineValue;
    view.engineText = entry.engineText;
    view.draftText = entry.draftText;
    view.draftValue = entry.draftValue;
    view.state = entry.state;
    view.missing = entry.missing;
    view.options = &entry.options;
    view.optionLabels = &entry.optionLabels;
    views.push_back(std::move(view));
  }
  return views;
}

int SettingsModel::rowCount() const {
  return static_cast<int>(Rows_.size());
}

bool SettingsModel::rowViewAt(int index, RowView &out) const {
  if (index < 0 || index >= static_cast<int>(Rows_.size()))
    return false;
  // The same projection rows() builds, for exactly one entry.
  const RowRuntime &entry = Rows_[static_cast<size_t>(index)];
  out.row = entry.row;
  out.engineValue = entry.engineValue;
  out.engineText = entry.engineText;
  out.draftText = entry.draftText;
  out.draftValue = entry.draftValue;
  out.state = entry.state;
  out.missing = entry.missing;
  out.options = &entry.options;
  out.optionLabels = &entry.optionLabels;
  return true;
}

std::optional<RowView> SettingsModel::find(const char *cvar) const {
  // Convenience single-row lookup over the same projection. Returned by
  // value: two finds must not alias one shared scratch object.
  const RowRuntime *entry = runtime(cvar);
  if (!entry)
    return std::nullopt;
  RowView view;
  view.row = entry->row;
  view.engineValue = entry->engineValue;
  view.engineText = entry->engineText;
  view.draftText = entry->draftText;
  view.draftValue = entry->draftValue;
  view.state = entry->state;
  view.missing = entry->missing;
  view.options = &entry->options;
  view.optionLabels = &entry->optionLabels;
  return view;
}

bool SettingsModel::isDirty() const {
  for (const auto &entry : Rows_) {
    if (entry.missing)
      continue;
    switch (entry.state) {
    case RowState::kDirty:
    case RowState::kInvalid:
    case RowState::kConflict:
    case RowState::kQueued:
    case RowState::kApplyFailed:
      return true;
    case RowState::kClean:
    case RowState::kMissing:
      break;
    }
  }
  return false;
}

bool SettingsModel::hasConflicts() const {
  for (const auto &entry : Rows_) {
    if (!entry.missing && entry.state == RowState::kConflict)
      return true;
  }
  return false;
}

bool SettingsModel::hasQueued() const {
  for (const auto &entry : Rows_) {
    if (!entry.missing && entry.state == RowState::kQueued)
      return true;
  }
  return false;
}

bool SettingsModel::editable(const char *Cvar) const {
  const auto *Entry = runtime(Cvar);
  if (!Entry || Entry->missing || Entry->row->ReadOnly)
    return false;
  const auto *Required = Entry->row->RequireCvar;
  return !Required || (Seams_.exists && Seams_.exists(Required) &&
                       (!Entry->row->RequireEnabled ||
                        (Seams_.readFloat && Seams_.readFloat(Required) > 0)));
}

} // namespace settings
} // namespace services
