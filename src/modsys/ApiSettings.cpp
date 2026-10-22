// framework::SettingsApi - the sdk settings section (the settings page's
// models, transactions, page tree and spray maker). See ApiTable.h for how it
// joins the module table.
#include "ApiTable.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <abcui/events.h>

#include "../services/settings/SettingsPageService.h"
#include "../services/uiconfig/UiDefinitions.h"

#include "EventBus.h"

namespace framework {
namespace SettingsApi {
namespace {

using services::settings::PageModels;

const PageModels &models() {
  return services::settings::pageModels();
}

void copyText(const std::string &text, char *out, uint32_t outSize) {
  if (!out || outSize == 0)
    return;
  uint32_t i = 0;
  for (; i + 1 < outSize && i < text.size(); ++i)
    out[i] = text[i];
  out[i] = '\0';
}

void copyText(const char *text, char *out, uint32_t outSize) {
  copyText(std::string(text ? text : ""), out, outSize);
}

// The caller's declared size wins: never write past an older header.
size_t declaredBytes(uint32_t declared, size_t full) {
  return (declared == 0 || declared > full) ? full : declared;
}

void bump() {
  services::settings::bumpPageRevision();
}

int32_t kindOf(services::settings::SettingKind kind) {
  using services::settings::SettingKind;
  switch (kind) {
  case SettingKind::kBool:
    return ABCUI_SET_KIND_BOOL;
  case SettingKind::kFloat:
    return ABCUI_SET_KIND_FLOAT;
  case SettingKind::kEnum:
    return ABCUI_SET_KIND_ENUM;
  case SettingKind::kNegate:
    return ABCUI_SET_KIND_NEGATE;
  case SettingKind::kText:
    return ABCUI_SET_KIND_TEXT;
  case SettingKind::kStringEnum:
    return ABCUI_SET_KIND_STRING_ENUM;
  case SettingKind::kCommandToggle:
    return ABCUI_SET_KIND_COMMAND_TOGGLE;
  }
  return ABCUI_SET_KIND_BOOL;
}

int32_t stateOf(services::settings::RowState state) {
  using services::settings::RowState;
  switch (state) {
  case RowState::kClean:
    return ABCUI_SET_STATE_CLEAN;
  case RowState::kDirty:
    return ABCUI_SET_STATE_DIRTY;
  case RowState::kInvalid:
    return ABCUI_SET_STATE_INVALID;
  case RowState::kConflict:
    return ABCUI_SET_STATE_CONFLICT;
  case RowState::kQueued:
    return ABCUI_SET_STATE_QUEUED;
  case RowState::kApplyFailed:
    return ABCUI_SET_STATE_APPLY_FAILED;
  case RowState::kMissing:
    return ABCUI_SET_STATE_MISSING;
  }
  return ABCUI_SET_STATE_CLEAN;
}

int32_t bindStateOf(services::settings::BindRowState state) {
  using services::settings::BindRowState;
  switch (state) {
  case BindRowState::kClean:
    return ABCUI_BIND_STATE_CLEAN;
  case BindRowState::kDirty:
    return ABCUI_BIND_STATE_DIRTY;
  case BindRowState::kQueued:
    return ABCUI_BIND_STATE_QUEUED;
  case BindRowState::kApplyFailed:
    return ABCUI_BIND_STATE_APPLY_FAILED;
  case BindRowState::kUnavailable:
    return ABCUI_BIND_STATE_UNAVAILABLE;
  }
  return ABCUI_BIND_STATE_UNAVAILABLE;
}

// ------------------------------------------------------------ cvar rows ----

uint32_t apiRevision(void) {
  return services::settings::pageRevision();
}

int32_t apiRowCount(void) {
  const auto *model = models().rows;
  return model ? model->rowCount() : 0;
}

bool fillRow(const services::settings::RowView &view, AbcUiSettingRow &full) {
  if (!view.row)
    return false;
  full.structSize = sizeof(AbcUiSettingRow);
  copyText(view.row->Cvar, full.cvar, sizeof(full.cvar));
  full.kind = kindOf(view.row->Kind);
  full.state = stateOf(view.state);
  full.flags = 0;
  if (view.row->Bounded)
    full.flags |= ABCUI_SET_ROW_BOUNDED;
  if (view.row->ReadOnly)
    full.flags |= ABCUI_SET_ROW_READ_ONLY;
  if (view.row->HueSwatch)
    full.flags |= ABCUI_SET_ROW_HUE_SWATCH;
  if (view.row->FileChoices)
    full.flags |= ABCUI_SET_ROW_FILE_CHOICES;
  if (view.row->HasDefault)
    full.flags |= ABCUI_SET_ROW_HAS_DEFAULT;
  if (view.row->RequireEnabled)
    full.flags |= ABCUI_SET_ROW_REQUIRE_ENABLED;
  if (view.missing)
    full.flags |= ABCUI_SET_ROW_MISSING;
  const auto *model = models().rows;
  if (model && model->editable(view.row->Cvar))
    full.flags |= ABCUI_SET_ROW_EDITABLE;
  full.optionCount = view.row->OptionCount;
  full.choiceCount =
      view.options ? static_cast<int32_t>(view.options->size()) : 0;
  full.minValue = view.row->Min;
  full.maxValue = view.row->Max;
  full.step = view.row->Step;
  full.defaultValue = view.row->DefaultValue;
  full.engineValue = view.engineValue;
  full.draftValue = view.draftValue;
  copyText(view.row->LabelToken ? view.row->LabelToken : "", full.labelToken,
           sizeof(full.labelToken));
  copyText(view.row->Section ? view.row->Section : "", full.section,
           sizeof(full.section));
  copyText(view.row->StateName ? view.row->StateName : "", full.stateName,
           sizeof(full.stateName));
  copyText(view.row->RequireCvar ? view.row->RequireCvar : "", full.requireCvar,
           sizeof(full.requireCvar));
  copyText(view.engineText, full.engineText, sizeof(full.engineText));
  copyText(view.draftText, full.draftText, sizeof(full.draftText));
  return true;
}

int32_t apiRowAt(int32_t index, AbcUiSettingRow *out) {
  const auto *model = models().rows;
  if (!model || !out)
    return 0;
  services::settings::RowView view;
  if (!model->rowViewAt(index, view))
    return 0;
  AbcUiSettingRow full{};
  if (!fillRow(view, full))
    return 0;
  std::memcpy(out, &full, declaredBytes(out->structSize, sizeof(full)));
  return 1;
}

int32_t apiRowFind(const char *cvar, AbcUiSettingRow *out) {
  const auto *model = models().rows;
  if (!model || !out || !cvar || !*cvar)
    return 0;
  const auto view = model->find(cvar);
  if (!view)
    return 0;
  AbcUiSettingRow full{};
  if (!fillRow(*view, full))
    return 0;
  std::memcpy(out, &full, declaredBytes(out->structSize, sizeof(full)));
  return 1;
}

int32_t apiOptionAt(const char *cvar, int32_t index, float *value, char *label,
                    uint32_t labelSize) {
  const auto *model = models().rows;
  if (!model || !cvar || !*cvar || index < 0)
    return 0;
  const auto view = model->find(cvar);
  if (!view || !view->row || index >= view->row->OptionCount)
    return 0;
  const services::settings::SettingEnumOption &option =
      view->row->Options[index];
  if (value)
    *value = option.Value;
  copyText(option.Label ? option.Label : "", label, labelSize);
  return 1;
}

int32_t apiChoiceAt(const char *cvar, int32_t index, char *value,
                    uint32_t valueSize, char *label, uint32_t labelSize) {
  const auto *model = models().rows;
  if (!model || !cvar || !*cvar || index < 0)
    return 0;
  const auto view = model->find(cvar);
  if (!view || !view->options ||
      index >= static_cast<int32_t>(view->options->size()))
    return 0;
  const auto &values = *view->options;
  const std::string &choice = values[static_cast<size_t>(index)];
  copyText(choice, value, valueSize);
  const bool hasLabel =
      view->optionLabels && view->optionLabels->size() == values.size();
  copyText(hasLabel ? (*view->optionLabels)[static_cast<size_t>(index)]
                    : std::string(),
           label, labelSize);
  return 1;
}

// ------------------------------------------------- transactions -----------

void apiSnapshot(void) {
  if (auto *model = models().rows)
    model->snapshot();
  if (auto *model = models().bind)
    model->snapshot();
  if (auto *model = models().video)
    model->snapshot();
  if (auto *model = models().voice)
    model->snapshot();
  bump();
}

void apiSnapshotRows(void) {
  if (auto *model = models().rows)
    model->snapshot();
  bump();
}

int32_t apiSetDraftText(const char *cvar, const char *text) {
  auto *model = models().rows;
  if (!model || !cvar || !*cvar)
    return 0;
  const bool ok = model->setDraftText(cvar, text ? text : "");
  bump();
  return ok ? 1 : 0;
}

int32_t apiSetDraftBool(const char *cvar, int32_t value) {
  auto *model = models().rows;
  if (!model || !cvar || !*cvar)
    return 0;
  const bool ok = model->setDraftBool(cvar, value != 0);
  bump();
  return ok ? 1 : 0;
}

int32_t apiSetDraftEnum(const char *cvar, float value) {
  auto *model = models().rows;
  if (!model || !cvar || !*cvar)
    return 0;
  const bool ok = model->setDraftEnum(cvar, value);
  bump();
  return ok ? 1 : 0;
}

int32_t apiSetDraftString(const char *cvar, const char *text) {
  auto *model = models().rows;
  if (!model || !cvar || !*cvar)
    return 0;
  const bool ok = model->setDraftString(cvar, text ? text : "");
  bump();
  return ok ? 1 : 0;
}

int32_t apiAdopt(const char *cvar) {
  auto *model = models().rows;
  if (!model || !cvar || !*cvar)
    return 0;
  const bool ok = model->adoptEngineValue(cvar);
  bump();
  return ok ? 1 : 0;
}

void apiResetDefaults(const char *const *cvars, int32_t count) {
  auto *model = models().rows;
  if (!model)
    return;
  if (cvars && count > 0) {
    std::vector<std::string> scope;
    scope.reserve(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; ++i) {
      if (cvars[i] && *cvars[i])
        scope.emplace_back(cvars[i]);
    }
    model->resetDefaults(scope);
  } else {
    model->resetDefaults();
  }
  bump();
}

void apiCancel(void) {
  if (auto *model = models().rows)
    model->cancel();
  if (auto *model = models().bind)
    model->cancel();
  if (auto *model = models().video)
    model->cancelStage();
  if (auto *model = models().voice) {
    model->stopTest();
    if (model->available())
      model->snapshot();
  }
  bump();
}

int32_t apiDirty(void) {
  const auto *model = models().rows;
  return model && model->isDirty() ? 1 : 0;
}

int32_t apiHasConflicts(void) {
  const auto *model = models().rows;
  return model && model->hasConflicts() ? 1 : 0;
}

int32_t apiEditable(const char *cvar) {
  const auto *model = models().rows;
  return model && cvar && *cvar && model->editable(cvar) ? 1 : 0;
}

int32_t apiHasQueued(void) {
  const auto *model = models().rows;
  return model && model->hasQueued() ? 1 : 0;
}

void apiApply(AbcUiSettingsApply *out) {
  AbcUiSettingsApply tally{};
  tally.structSize = sizeof(AbcUiSettingsApply);
  if (auto *model = models().rows) {
    for (const auto &result : model->apply()) {
      switch (result.outcome) {
      case services::settings::ApplyOutcome::kApplied:
        ++tally.applied;
        break;
      case services::settings::ApplyOutcome::kQueued:
        ++tally.queued;
        break;
      default:
        ++tally.failed;
        break;
      }
    }
  }
  bump();
  if (out)
    std::memcpy(out, &tally, declaredBytes(out->structSize, sizeof(tally)));
}

int32_t apiApplyOne(const char *cvar) {
  auto *model = models().rows;
  if (!model || !cvar || !*cvar)
    return 0;
  const auto result = model->applyOne(cvar);
  bump();
  return result ? 1 : 0;
}

// ---------------------------------------------------------- page tree ----

const std::vector<services::uiconfig::SettingsPageDefinition> *pages() {
  return models().pages;
}

const services::uiconfig::SettingsPageDefinition *pageAt(int32_t index) {
  const auto *tree = pages();
  if (!tree || index < 0 || index >= static_cast<int32_t>(tree->size()))
    return nullptr;
  return &(*tree)[static_cast<size_t>(index)];
}

int32_t apiPageCount(void) {
  const auto *tree = pages();
  return tree ? static_cast<int32_t>(tree->size()) : 0;
}

int32_t apiPageAt(int32_t index, AbcUiSettingsPage *out) {
  const auto *page = pageAt(index);
  if (!page || !out)
    return 0;
  AbcUiSettingsPage full{};
  full.structSize = sizeof(AbcUiSettingsPage);
  full.sectionCount = static_cast<int32_t>(page->Sections.size());
  full.cvarCount = static_cast<int32_t>(page->Cvars.size());
  copyText(page->Id, full.id, sizeof(full.id));
  copyText(page->Label, full.label, sizeof(full.label));
  copyText(page->Description, full.description, sizeof(full.description));
  std::memcpy(out, &full, declaredBytes(out->structSize, sizeof(full)));
  return 1;
}

int32_t apiPageCvarAt(int32_t page, int32_t index, char *out,
                      uint32_t outSize) {
  const auto *definition = pageAt(page);
  if (!definition || index < 0 ||
      index >= static_cast<int32_t>(definition->Cvars.size()))
    return 0;
  copyText(definition->Cvars[static_cast<size_t>(index)], out, outSize);
  return 1;
}

int32_t apiSectionAt(int32_t page, int32_t index, AbcUiSettingsSection *out) {
  const auto *definition = pageAt(page);
  if (!definition || !out || index < 0 ||
      index >= static_cast<int32_t>(definition->Sections.size()))
    return 0;
  const auto &section = definition->Sections[static_cast<size_t>(index)];
  AbcUiSettingsSection full{};
  full.structSize = sizeof(AbcUiSettingsSection);
  full.componentCount = static_cast<int32_t>(section.Components.size());
  copyText(section.Id, full.id, sizeof(full.id));
  copyText(section.Label, full.label, sizeof(full.label));
  copyText(section.Layout, full.layout, sizeof(full.layout));
  std::memcpy(out, &full, declaredBytes(out->structSize, sizeof(full)));
  return 1;
}

int32_t apiSectionComponentAt(int32_t page, int32_t section, int32_t index,
                              char *out, uint32_t outSize) {
  const auto *definition = pageAt(page);
  if (!definition || section < 0 ||
      section >= static_cast<int32_t>(definition->Sections.size()))
    return 0;
  const auto &components =
      definition->Sections[static_cast<size_t>(section)].Components;
  if (index < 0 || index >= static_cast<int32_t>(components.size()))
    return 0;
  copyText(components[static_cast<size_t>(index)], out, outSize);
  return 1;
}

int32_t apiPageHasComponent(int32_t page, const char *component) {
  const auto *definition = pageAt(page);
  if (!definition || !component || !*component)
    return 0;
  return definition->hasComponent(component) ||
                 definition->sectionHaving(component)
             ? 1
             : 0;
}

// -------------------------------------------------------- key bindings ----

int32_t apiBindReadsAvailable(void) {
  const auto *model = models().bind;
  return model && model->readsAvailable() ? 1 : 0;
}

int32_t apiBindRowCount(void) {
  const auto *model = models().bind;
  return model ? model->rowCount() : 0;
}

int32_t apiBindRowAt(int32_t index, AbcUiBindAction *out) {
  const auto *model = models().bind;
  if (!model || !out)
    return 0;
  const auto view = model->find(index);
  if (!view || !view->action || index < 0)
    return 0;
  AbcUiBindAction full{};
  full.structSize = sizeof(AbcUiBindAction);
  full.index = view->actionIndex;
  full.header = view->header ? 1 : 0;
  full.state = bindStateOf(view->state);
  full.keyCount = static_cast<int32_t>(view->draftKeyNames.size());
  copyText(view->action->Label, full.label, sizeof(full.label));
  copyText(view->action->Command, full.command, sizeof(full.command));
  std::memcpy(out, &full, declaredBytes(out->structSize, sizeof(full)));
  return 1;
}

int32_t apiBindKeyAt(int32_t actionIndex, int32_t index, char *out,
                     uint32_t outSize) {
  const auto *model = models().bind;
  if (!model)
    return 0;
  const auto view = model->find(actionIndex);
  if (!view || index < 0 ||
      index >= static_cast<int32_t>(view->draftKeyNames.size()))
    return 0;
  copyText(view->draftKeyNames[static_cast<size_t>(index)], out, outSize);
  return 1;
}

int32_t apiBindLookupLabel(const char *token, char *out, uint32_t outSize) {
  const auto *model = models().bind;
  if (!model || !token || !*token)
    return 0;
  std::string text;
  if (!model->lookupLabel(token, text))
    return 0;
  copyText(text, out, outSize);
  return 1;
}

int32_t apiBindCapturing(void) {
  const auto *model = models().bind;
  return model && model->capturing() ? model->captureRow() : -1;
}

int32_t apiBindBeginCapture(int32_t actionIndex) {
  const services::settings::PageAccess &seam = services::settings::pageAccess();
  if (!seam.beginBindCapture)
    return 0;
  const int32_t result = seam.beginBindCapture(actionIndex) ? 1 : 0;
  bump();
  return result;
}

int32_t apiBindConflictPending(void) {
  const auto *model = models().bind;
  return model && model->conflictPending() ? 1 : 0;
}

int32_t apiBindConflictKeyName(char *out, uint32_t outSize) {
  const auto *model = models().bind;
  if (!model || !model->conflictPending())
    return 0;
  copyText(model->conflictKeyName(), out, outSize);
  return 1;
}

int32_t apiBindConflictOtherLabel(char *out, uint32_t outSize) {
  const auto *model = models().bind;
  if (!model || !model->conflictPending())
    return 0;
  copyText(model->conflictOtherLabel(), out, outSize);
  return 1;
}

void apiBindResolveConflict(int32_t accept) {
  auto *model = models().bind;
  if (!model)
    return;
  model->resolveConflict(accept != 0);
  bump();
}

void apiBindClear(int32_t actionIndex) {
  auto *model = models().bind;
  if (!model)
    return;
  model->clearActionDraft(actionIndex);
  bump();
}

void apiBindRemove(int32_t actionIndex, const char *keyName) {
  auto *model = models().bind;
  if (!model || !keyName || !*keyName)
    return;
  model->removeKeyFromAction(actionIndex, keyName);
  bump();
}

void apiBindResetDefaults(void) {
  auto *model = models().bind;
  if (!model)
    return;
  model->resetDefaults();
  bump();
}

int32_t apiBindDirty(void) {
  const auto *model = models().bind;
  return model && model->isDirty() ? 1 : 0;
}

int32_t apiBindHasQueued(void) {
  const auto *model = models().bind;
  return model && model->hasQueued() ? 1 : 0;
}

void apiBindApply(void) {
  auto *model = models().bind;
  if (!model)
    return;
  model->apply();
  bump();
}

int32_t apiBindVerify(void) {
  auto *model = models().bind;
  if (!model)
    return 0;
  const bool changed = model->verify();
  if (changed)
    bump();
  return changed ? 1 : 0;
}

// ------------------------------------------------------ display modes ----

int32_t apiVideoFacts(AbcUiVideoFacts *out) {
  auto *model = models().video;
  if (!model || !out)
    return 0;
  AbcUiVideoFacts full{};
  full.structSize = sizeof(AbcUiVideoFacts);
  full.available = model->available() ? 1 : 0;
  full.actualKnown = model->actualKnown() ? 1 : 0;
  full.awaitingConfirm = model->awaitingConfirm() ? 1 : 0;
  full.hasStaged = model->hasStaged() ? 1 : 0;
  full.remainingSeconds = model->remainingSeconds();
  full.modeCount = static_cast<int32_t>(model->modes().size());
  const auto &actual = model->actual();
  full.actualWidth = actual.Width;
  full.actualHeight = actual.Height;
  full.actualWindowed = actual.Windowed ? 1 : 0;
  const auto staged = model->stagedTarget();
  full.stagedWidth = staged.Width;
  full.stagedHeight = staged.Height;
  full.stagedWindowed = staged.Windowed ? 1 : 0;
  if (full.awaitingConfirm) {
    const auto &record = model->pending();
    full.requestedWidth = record.NewWidth;
    full.requestedHeight = record.NewHeight;
    full.requestedWindowed = record.NewWindowed;
    // confirmKind() re-queries the engine's actual mode: only ask while a
    // confirmation actually owns the screen.
    switch (model->confirmKind()) {
    case services::settings::VideoModeModel::ConfirmKind::kApplied:
      full.confirmKind = ABCUI_VIDEO_CONFIRM_APPLIED;
      break;
    case services::settings::VideoModeModel::ConfirmKind::kNotApplied:
      full.confirmKind = ABCUI_VIDEO_CONFIRM_NOT_APPLIED;
      break;
    case services::settings::VideoModeModel::ConfirmKind::kUnknown:
      full.confirmKind = ABCUI_VIDEO_CONFIRM_UNKNOWN;
      break;
    }
  }
  std::memcpy(out, &full, declaredBytes(out->structSize, sizeof(full)));
  return 1;
}

int32_t apiVideoModeAt(int32_t index, int32_t *width, int32_t *height) {
  const auto *model = models().video;
  if (!model || index < 0 ||
      index >= static_cast<int32_t>(model->modes().size()))
    return 0;
  const auto &mode = model->modes()[static_cast<size_t>(index)];
  if (width)
    *width = mode.Width;
  if (height)
    *height = mode.Height;
  return 1;
}

int32_t apiVideoStageResolution(int32_t width, int32_t height) {
  auto *model = models().video;
  if (!model)
    return 0;
  const bool ok = model->stageResolution(width, height);
  bump();
  return ok ? 1 : 0;
}

int32_t apiVideoStageWindowed(int32_t windowed) {
  auto *model = models().video;
  if (!model)
    return 0;
  const bool ok = model->stageWindowed(windowed != 0);
  bump();
  return ok ? 1 : 0;
}

void apiVideoCancelStage(void) {
  if (auto *model = models().video)
    model->cancelStage();
  bump();
}

int32_t apiVideoApply(void) {
  const services::settings::PageAccess &seam = services::settings::pageAccess();
  const int32_t result = seam.videoApply && seam.videoApply() ? 1 : 0;
  bump();
  return result;
}

int32_t apiVideoKeep(void) {
  const services::settings::PageAccess &seam = services::settings::pageAccess();
  const int32_t result = seam.videoKeep && seam.videoKeep() ? 1 : 0;
  bump();
  return result;
}

int32_t apiVideoRevert(void) {
  const services::settings::PageAccess &seam = services::settings::pageAccess();
  const int32_t result = seam.videoRevert && seam.videoRevert() ? 1 : 0;
  bump();
  return result;
}

int32_t apiVideoDescribe(int32_t width, int32_t height, int32_t windowed,
                         char *out, uint32_t outSize) {
  services::settings::VideoMode mode;
  mode.Width = width;
  mode.Height = height;
  mode.Windowed = windowed != 0;
  copyText(services::settings::VideoModeModel::describe(mode), out, outSize);
  return 1;
}

// ------------------------------------------------- microphone hardware ----

int32_t apiVoiceFacts(AbcUiVoiceFacts *out) {
  const auto *model = models().voice;
  if (!model || !out)
    return 0;
  AbcUiVoiceFacts full{};
  full.structSize = sizeof(AbcUiVoiceFacts);
  full.available = model->available() ? 1 : 0;
  full.testing = model->testing() ? 1 : 0;
  full.micBoost = model->micBoost() ? 1 : 0;
  full.micBoostDraft = model->micBoostDraft() ? 1 : 0;
  full.micBoostDirty = model->micBoostDirty() ? 1 : 0;
  full.micDirty = model->micDirty() ? 1 : 0;
  full.micVolume = model->micVolume();
  full.micVolumeDraft = model->micVolumeDraft();
  full.speakingLevel = model->speakingLevel();
  std::memcpy(out, &full, declaredBytes(out->structSize, sizeof(full)));
  return 1;
}

void apiVoiceSetVolumeDraft(float value) {
  if (auto *model = models().voice)
    model->setMicVolumeDraft(value);
  bump();
}

void apiVoiceSetBoostDraft(int32_t on) {
  if (auto *model = models().voice)
    model->setMicBoostDraft(on != 0);
  bump();
}

void apiVoiceStartTest(void) {
  if (auto *model = models().voice)
    model->startTest();
  bump();
}

void apiVoiceStopTest(void) {
  if (auto *model = models().voice)
    model->stopTest();
  bump();
}

int32_t apiVoiceApply(void) {
  auto *model = models().voice;
  if (!model || !model->available() || !model->isDirty())
    return -1;
  // Per-control apply: a refused device write keeps the draft.
  bool applied = model->applyMicVolume();
  applied = model->applyMicBoost() && applied;
  bump();
  return applied ? 1 : 0;
}

// ------------------------------------------------ player-model favorites --

int32_t apiFavoritesAvailable(void) {
  const auto *model = models().favorites;
  return model && model->savesAllowed() ? 1 : 0;
}

uint32_t apiFavoritesGeneration(void) {
  return services::settings::favoritesGeneration();
}

int32_t apiFavoriteCount(void) {
  const auto *model = models().favorites;
  return model ? static_cast<int32_t>(model->favorites().size()) : 0;
}

int32_t apiFavoriteAt(int32_t index, char *out, uint32_t outSize) {
  const auto *model = models().favorites;
  if (!model || index < 0 ||
      index >= static_cast<int32_t>(model->favorites().size()))
    return 0;
  copyText(model->favorites()[static_cast<size_t>(index)], out, outSize);
  return 1;
}

int32_t apiFavoriteContains(const char *model) {
  const auto *favorites = models().favorites;
  if (!favorites || !model || !*model)
    return 0;
  return favorites->isFavorite(model) ? 1 : 0;
}

void apiFavoriteSet(const char *model, int32_t favorite) {
  auto *favorites = models().favorites;
  if (!favorites || !model || !*model || !favorites->savesAllowed())
    return;
  favorites->setFavorite(model, favorite != 0);
  services::settings::bumpFavoritesGeneration();
}

// ---------------------------------------------------------- spray maker ---

int32_t apiSprayMake(const char *path) {
  const services::settings::PageAccess &seam = services::settings::pageAccess();
  if (!seam.sprayMake || !path || !*path)
    return 0;
  return seam.sprayMake(path) ? 1 : 0;
}

int32_t apiSprayStatus(char *out, uint32_t outSize) {
  copyText(services::settings::sprayStatus(), out, outSize);
  return 1;
}

int32_t apiSprayPreviewUri(char *out, uint32_t outSize) {
  copyText(services::settings::sprayPreviewUri(), out, outSize);
  return 1;
}

const AbcUiSettingsApi kSettingsApi = {
    sizeof(AbcUiSettingsApi),
    apiRevision,
    apiRowCount,
    apiRowAt,
    apiRowFind,
    apiOptionAt,
    apiChoiceAt,
    apiSnapshot,
    apiSnapshotRows,
    apiSetDraftText,
    apiSetDraftBool,
    apiSetDraftEnum,
    apiSetDraftString,
    apiAdopt,
    apiResetDefaults,
    apiCancel,
    apiDirty,
    apiHasConflicts,
    apiEditable,
    apiHasQueued,
    apiApply,
    apiApplyOne,
    apiPageCount,
    apiPageAt,
    apiPageCvarAt,
    apiSectionAt,
    apiSectionComponentAt,
    apiPageHasComponent,
    apiBindReadsAvailable,
    apiBindRowCount,
    apiBindRowAt,
    apiBindKeyAt,
    apiBindLookupLabel,
    apiBindCapturing,
    apiBindBeginCapture,
    apiBindConflictPending,
    apiBindConflictKeyName,
    apiBindConflictOtherLabel,
    apiBindResolveConflict,
    apiBindClear,
    apiBindRemove,
    apiBindResetDefaults,
    apiBindDirty,
    apiBindHasQueued,
    apiBindApply,
    apiBindVerify,
    apiVideoFacts,
    apiVideoModeAt,
    apiVideoStageResolution,
    apiVideoStageWindowed,
    apiVideoCancelStage,
    apiVideoApply,
    apiVideoKeep,
    apiVideoRevert,
    apiVideoDescribe,
    apiVoiceFacts,
    apiVoiceSetVolumeDraft,
    apiVoiceSetBoostDraft,
    apiVoiceStartTest,
    apiVoiceStopTest,
    apiVoiceApply,
    apiFavoritesAvailable,
    apiFavoritesGeneration,
    apiFavoriteCount,
    apiFavoriteAt,
    apiFavoriteContains,
    apiFavoriteSet,
    apiSprayMake,
    apiSprayStatus,
    apiSprayPreviewUri,
};

} // namespace

const AbcUiSettingsApi *get() {
  return &kSettingsApi;
}

bool consumeEscape() {
  AbcUiKeyEvent event{};
  event.down = 1;
  event.keynum = ABCUI_KEY_ESCAPE;
  return eventBus().publishVetoRaw(ABCUI_CHANNEL_SETTINGS_ESCAPE, &event);
}

void bumpRevision() {
  bump();
}

} // namespace SettingsApi
} // namespace framework
