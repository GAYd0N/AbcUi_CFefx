// The settings page (moved from panels::settings) and the display-mode
// confirmation dialog: the last framework pages.
//
// The page is a PROJECTION of the sdk settings section: the cvar rows, the
// key bindings, the display modes, the microphone hardware, the JSON page
// tree, the favorite store and the spray maker all stay framework-side. The
// page owns its DOM, its page-internal layers and its local gestures; every
// model edit goes through the section and every navigation intent through the
// menu section - the page itself never writes a cvar or runs a command.
//
// Presentation follows the same rule as the other pages: menu.state decides
// whether the page is on top, menu.paint re-projects it. The page also owns
// the display-mode confirmation document, presented from the video facts
// (awaitingConfirm) on the same paint beat; the framework's video transaction
// keeps the record, the restart ordering and the timeout.
//
// Page-internal layers (reset confirmation, unsaved-changes prompt, bind
// conflict, bind defaults confirmation, spray picker) are DOM overlays inside
// this document, not controller overlays. The menu owner's routed ESC asks
// the page through the settings.escape channel: a page that consumed the
// press (a layer is up, a capture session or a favorite menu is open, drafts
// are pending) answers nonzero and the menu re-syncs instead of popping.
//
// Rendering rules: every dynamic string reaches the DOM through
// ui->translate + ui->escapeRml (draft text is user input); all layout is dp.
// Visibility of the layers is tracked in page members, never inferred from
// the document.
//
// This translation unit is the page shell: the page state, the shared
// DOM/model helpers, the layers, the page structure/navigation, the
// apply/reset/leave transactions, the render orchestrator, the document
// lifecycle and the public load/shutdown/unload entries. The per-widget
// projections live in the sibling translation units behind SettingsInternal.h.

#include "../shared/PageShared.h"
#include "SettingsInternal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include <string_view>

namespace settingspage {

AbcUiDocument *Page = nullptr;
AbcUiDocument *Confirm = nullptr;
AbcUiElement *Root = nullptr;
AbcUiElement *ConfirmRoot = nullptr;
uint64_t ClickBinding = 0;
uint64_t ChangeBinding = 0;
uint64_t DblClickBinding = 0;
uint64_t MouseDownBinding = 0;
uint64_t ConfirmClickBinding = 0;
AbcUiSubscription StateSubscription = 0;
AbcUiSubscription PaintSubscription = 0;
AbcUiSubscription EscapeSubscription = 0;

// Page structure: the navigation and the row skeletons are built once per
// document (a rebuild drops hover/focus), then only re-projected.
bool Built = false;
bool Presented = false;
std::string ActivePage;
// "" when no layer is up, else the layer's DOM id.
std::string ActiveLayer;
// Re-entrancy guard: a projection that rewrites a select's options dispatches
// change while it runs; that programmatic churn is not user input.
bool InRender = false;
uint32_t RenderedRevision = ~0u;
// Counts the skeleton DOM was last built from. The document can become ready
// before the framework models are loaded (the page is created at context
// ready, while the models load from onInit), so the first build may see an
// empty bind list; render() rebuilds when a count moves.
int BuiltRowCount = -1;
int BuiltBindCount = -1;
int BuiltPageCount = -1;
// The generation last projected into the confirmation dialog (its facts
// re-query the engine, so the dialog re-renders on change only).
uint32_t ConfirmRenderedRevision = ~0u;

// Option sets last projected into select rows (cvar -> value/label pairs): the
// widget is rebuilt only when the set actually changed.
std::vector<std::pair<std::string, std::vector<std::string>>> SelectShown;
// The resolution options last projected into the display select.
std::vector<std::pair<int, int>> VideoOptionsShown;
// The hue last painted on one row's drag thumb (cvar -> "rgb(...)").
std::vector<std::pair<std::string, std::string>> HueShown;
// The element of the voice-volume change commit in flight (its caret/IME
// state must survive the status refresh).
AbcUiElement *VoiceVolumeSkip = nullptr;

// Model picker page state (the filter and the favorites-only toggle live as
// long as the document).
std::string ModelFilter;
bool ModelFavOnly = false;
std::string ModelBuiltSignature;
uint32_t ModelBuiltFavoriteGeneration = 0;
bool ModelContextOpen = false;
std::string ModelContextModel;

// Spray picker page state (the directory model; the maker runs framework-side).
files::FileBrowser Browser;
bool SprayOpen = false;
std::string SpraySelected;

// The ten part elements of the crosshair preview and the state last applied
// to each (a per-change render must not rewrite ten elements' properties when
// nothing moved).

PreviewPart PreviewCore[5];
PreviewPart PreviewOutline[5];

// ------------------------------------------------------------- helpers ----

AbcUiElement *node(const char *id) {
  return Page ? Api->ui->querySelector(Page, id) : nullptr;
}

AbcUiElement *node(const std::string &id) {
  return node(id.c_str());
}

AbcUiElement *confirmNode(const char *id) {
  return Confirm ? Api->ui->querySelector(Confirm, id) : nullptr;
}

void setInnerRml(AbcUiElement *element, const std::string &rml) {
  if (element)
    Api->ui->elementSetInnerRML(element, rml.c_str());
}

void setInnerRml(const char *id, const std::string &rml) {
  setInnerRml(node(id), rml);
}

void setText(const char *id, const std::string &text) {
  setInnerRml(id, escapeText(text));
}

void setDisplay(AbcUiElement *element, bool visible) {
  if (element)
    Api->ui->elementSetProperty(element, "display", visible ? "block" : "none");
}

void setDisplay(const char *id, bool visible) {
  setDisplay(node(id), visible);
}

void setProperty(AbcUiElement *element, const char *name, const char *value) {
  if (element && value)
    Api->ui->elementSetProperty(element, name, value);
}

std::string numberText(float value) {
  return std::format("{:g}", value);
}

bool isOn(float value) {
  return value > 0.5f;
}

bool parseNumber(const char *text, float &out) {
  if (!text || !*text)
    return false;
  const auto parsed = parseFloat(text);
  if (!parsed)
    return false;
  out = *parsed;
  return true;
}

bool isToggleKind(int32_t kind) {
  return kind == ABCUI_SET_KIND_BOOL || kind == ABCUI_SET_KIND_NEGATE ||
         kind == ABCUI_SET_KIND_COMMAND_TOGGLE;
}

bool isSelectKind(int32_t kind) {
  return kind == ABCUI_SET_KIND_ENUM || kind == ABCUI_SET_KIND_STRING_ENUM;
}

bool toggleDraftOn(const AbcUiSettingRow &row) {
  if (row.kind == ABCUI_SET_KIND_NEGATE)
    return row.draftValue < 0.0f;
  return isOn(row.draftValue);
}

// The engine stores cl_logocolor as a localized token
// ("#abcui.gameui/Valve_Ltblue") while the shipped option table uses bare names
// ("Ltblue"); both spellings select the same spray colour, so labels and the
// preview URI normalize to the bare name. Other values pass through unchanged.
std::string logoColorName(const std::string &value) {
  std::string name = value;
  if (!name.empty() && name[0] == '#')
    name.erase(0, 1);
  constexpr const char *kValvePrefix = "Valve_";
  if (name.starts_with(kValvePrefix))
    name.erase(0, std::strlen(kValvePrefix));
  return name;
}

// The dropdown label for a file-backed choice: the file name without its
// directory or extension (the value keeps the full relative path).
std::string fileStem(const std::string &value) {
  const std::size_t slash = value.find_last_of("/\\");
  const std::size_t begin = slash == std::string::npos ? 0 : slash + 1;
  const std::size_t dot = value.find_last_of('.');
  const std::size_t end =
      dot != std::string::npos && dot > begin ? dot : value.size();
  return value.substr(begin, end - begin);
}

const char *safeFocusForLayer(const char *id) {
  if (std::string_view(id) == kLayerReset)
    return "reset-no";
  if (std::string_view(id) == kLayerBindConflict)
    return "bind-conflict-no";
  if (std::string_view(id) == kLayerBindReset)
    return "bind-reset-no";
  if (std::string_view(id) == kLayerSprayFile)
    return "spray-file-cancel";
  return "leave-stay";
}

bool layerVisible() {
  return !ActiveLayer.empty();
}

bool rowFound(const char *cvar, AbcUiSettingRow &out) {
  if (!Api->settings || !cvar || !*cvar)
    return false;
  out.structSize = sizeof(out);
  return Api->settings->rowFind(cvar, &out) != 0;
}

bool rowById(int32_t index, AbcUiSettingRow &out) {
  if (!Api->settings)
    return false;
  out.structSize = sizeof(out);
  return Api->settings->rowAt(index, &out) != 0;
}

bool videoFacts(AbcUiVideoFacts &out) {
  out.structSize = sizeof(out);
  return Api->settings && Api->settings->videoFacts(&out) != 0;
}

bool voiceFacts(AbcUiVoiceFacts &out) {
  out.structSize = sizeof(out);
  return Api->settings && Api->settings->voiceFacts(&out) != 0;
}

bool currentVideoStaged() {
  AbcUiVideoFacts facts{};
  return videoFacts(facts) && facts.hasStaged != 0;
}

bool currentVoiceDirty() {
  AbcUiVoiceFacts facts{};
  return voiceFacts(facts) && (facts.micDirty || facts.micBoostDirty);
}

bool isDirty() {
  if (!Api->settings)
    return false;
  return Api->settings->dirty() != 0 || Api->settings->bindDirty() != 0 ||
         currentVideoStaged() || currentVoiceDirty();
}

// -------------------------------------------------------------- layers ----

void hideLayers() {
  for (const char *id : {kLayerReset, kLayerLeave, kLayerBindConflict,
                         kLayerBindReset, kLayerSprayFile}) {
    setDisplay(id, false);
  }
  ActiveLayer.clear();
  if (SprayOpen) {
    SprayOpen = false;
    SpraySelected.clear();
    renderSprayBrowser();
  }
}

void showLayer(const char *id) {
  if (!Page)
    return;
  hideLayers();
  if (AbcUiElement *layer = node(id)) {
    setDisplay(layer, true);
    ActiveLayer = id;
  }
  // autofocus only rides the document Show; a layer opened later takes its
  // safe default explicitly (cancel/stay, never the destructive entry).
  if (AbcUiElement *focus = node(safeFocusForLayer(id)))
    Api->ui->elementFocus(focus);
}

void refocusDefault() {
  if (!Page)
    return;
  if (!ActiveLayer.empty()) {
    if (AbcUiElement *focus = node(safeFocusForLayer(ActiveLayer.c_str())))
      Api->ui->elementFocus(focus);
    return;
  }
  if (AbcUiElement *focus = node("settings-apply"))
    Api->ui->elementFocus(focus);
}

// ------------------------------------------------------ page structure ----

int pageCount() {
  return Api->settings ? Api->settings->pageCount() : 0;
}

bool pageAt(int32_t index, AbcUiSettingsPage &out) {
  out.structSize = sizeof(out);
  return Api->settings && Api->settings->pageAt(index, &out) != 0;
}

bool sectionAt(int32_t page, int32_t index, AbcUiSettingsSection &out) {
  out.structSize = sizeof(out);
  return Api->settings && Api->settings->sectionAt(page, index, &out) != 0;
}

std::string sectionComponent(int32_t page, int32_t section, int32_t index) {
  char buffer[ABCUI_SETTINGS_ID_MAX];
  buffer[0] = '\0';
  if (!Api->settings || !Api->settings->sectionComponentAt(
                            page, section, index, buffer, sizeof(buffer)))
    return {};
  return buffer;
}

std::string pageCvar(int32_t page, int32_t index) {
  char buffer[ABCUI_SETTINGS_CVAR_MAX];
  buffer[0] = '\0';
  if (!Api->settings ||
      !Api->settings->pageCvarAt(page, index, buffer, sizeof(buffer)))
    return {};
  return buffer;
}

int pageIndexOf(const std::string &id) {
  for (int32_t index = 0; index < pageCount(); ++index) {
    AbcUiSettingsPage page{};
    if (pageAt(index, page) && id == page.id)
      return index;
  }
  return -1;
}

bool pageHasComponent(int32_t page, const char *name) {
  return Api->settings && Api->settings->pageHasComponent(page, name) != 0;
}

// Unconfigured runs (no page tree) retain the legacy all-in-one projection.
bool hasComponent(const char *name) {
  if (pageCount() == 0)
    return true;
  const int page = pageIndexOf(ActivePage);
  return page >= 0 && pageHasComponent(page, name);
}

bool sectionHasComponent(int32_t page, int32_t section, const char *name) {
  AbcUiSettingsSection definition{};
  if (!sectionAt(page, section, definition))
    return false;
  for (int32_t index = 0; index < definition.componentCount; ++index)
    if (sectionComponent(page, section, index) == name)
      return true;
  return false;
}

bool ownsColorCvar(const std::string &cvar) {
  for (int i = 0; i < kColorBindingCount; ++i) {
    const ColorBinding &binding = kColorBindings[i];
    if (cvar == binding.R || cvar == binding.G || cvar == binding.B ||
        cvar == binding.A)
      return true;
  }
  return false;
}

void projectPage() {
  if (!Page)
    return;
  closeModelContextMenu();
  const int active = pageIndexOf(ActivePage);
  for (int32_t index = 0; index < pageCount(); ++index) {
    AbcUiSettingsPage page{};
    if (!pageAt(index, page))
      continue;
    const bool selected = index == active;
    setDisplay(node("settings-rows-" + std::string(page.id)), selected);
    if (AbcUiElement *tab = node("settings-tab-" + std::string(page.id)))
      Api->ui->elementSetClass(tab, "selected", selected);
  }
  // The key-binding defaults entry lives in the global action bar and is only
  // offered where the page declares the bind list.
  setDisplay("bind-reset", hasComponent("bindings"));
  AbcUiSettingsPage current{};
  const bool havePage = active >= 0 && pageAt(active, current);
  setText("title", havePage ? resolve(current.label)
                            : resolve("#abcui.gameui/Settings_Title"));
  setText("settings-description",
          havePage ? resolve(current.description) : std::string());
  setDisplay("settings-reset", havePage && current.cvarCount > 0);
  // The singleton widget blocks (bindings, video, voice, spray, previews)
  // live in the parking host until the active page claims them. Moving them
  // also has to happen on the first projection, so the bind card is not left
  // empty while its block sits in the hidden host.
  projectWidgets();
}

void buildNavigation() {
  if (!Page || pageCount() == 0)
    return;
  AbcUiElement *navigation = node("settings-navigation");
  if (!navigation)
    return;
  std::string markup;
  for (int32_t index = 0; index < pageCount(); ++index) {
    AbcUiSettingsPage page{};
    if (!pageAt(index, page))
      continue;
    markup += "<button id=\"settings-tab-" + std::string(page.id) +
              "\" data-action=\"settings-page\" data-page=\"" +
              escapeAttribute(page.id) + "\">" +
              escapeText(resolve(page.label)) + "</button>";
  }
  Api->ui->elementSetInnerRML(navigation, markup.c_str());
  if (pageIndexOf(ActivePage) < 0) {
    AbcUiSettingsPage first{};
    ActivePage = pageAt(0, first) ? first.id : std::string();
  }
  projectPage();
}

bool selectPage(const std::string &id) {
  const int page = pageIndexOf(id);
  if (page < 0)
    return false;
  if (layerVisible() || Api->settings->bindCapturing() >= 0)
    return false;
  if (id != ActivePage) {
    Api->settings->voiceStopTest();
    renderVoiceSection();
  }
  ActivePage = id;
  projectPage();
  if (AbcUiElement *panel = node("panel"))
    Api->ui->elementSetScrollTop(panel, 0.0f);
  if (AbcUiElement *tab = node("settings-tab-" + id))
    Api->ui->elementFocus(tab);
  return true;
}

// --------------------------------------------------------------- rows -----

// --------------------------------------------------------- transactions ----

void applyAndReport() {
  if (!Page || !Api->settings)
    return;
  AbcUiSettingsApply tally{};
  tally.structSize = sizeof(tally);
  Api->settings->apply(&tally);
  // Bind writes ride the engine command buffer: applied rows already match
  // the truth, queued rows resolve on the next frames (the page verifies per
  // beat and re-renders on change).
  Api->settings->bindApply();
  const int32_t bindTouched = Api->settings->bindDirty() ? 1 : 0;
  int32_t applied = tally.applied;
  int32_t failed = tally.failed;
  const int32_t queued = tally.queued;
  // Per-control apply like the model rows: each microphone control reports
  // its own outcome, a refused device write keeps the draft.
  const int32_t voice = Api->settings->voiceApply();
  if (voice >= 0) {
    if (voice)
      ++applied;
    else
      ++failed;
  }
  const int32_t touched = applied + failed + queued + bindTouched;
  std::string text;
  if (touched == 0) {
    text = resolve("#abcui.gameui/Settings_StatusClean");
  } else if (failed == 0) {
    text = resolve("#abcui.gameui/Settings_Applied");
    if (queued > 0)
      text += " | " + std::to_string(queued) + " " +
              resolve("#abcui.gameui/Settings_QueuedPlural");
  } else {
    text = std::to_string(applied) + "/" + std::to_string(applied + failed) +
           " " + resolve("#abcui.gameui/Settings_ApplyPartial");
  }
  setInnerRml(node("settings-status"), escapeText(text));
  render();
}

void resetCurrentPage() {
  if (!Api->settings)
    return;
  const int page = pageIndexOf(ActivePage);
  if (page < 0) {
    Api->settings->resetDefaults(nullptr, 0);
    return;
  }
  AbcUiSettingsPage definition{};
  if (!pageAt(page, definition)) {
    Api->settings->resetDefaults(nullptr, 0);
    return;
  }
  std::vector<std::string> cvars;
  cvars.reserve(static_cast<std::size_t>(definition.cvarCount));
  for (int32_t index = 0; index < definition.cvarCount; ++index) {
    const std::string cvar = pageCvar(page, index);
    if (!cvar.empty())
      cvars.push_back(cvar);
  }
  std::vector<const char *> pointers;
  pointers.reserve(cvars.size());
  for (const auto &cvar : cvars)
    pointers.push_back(cvar.c_str());
  Api->settings->resetDefaults(pointers.empty() ? nullptr : pointers.data(),
                               static_cast<int32_t>(pointers.size()));
  // Hardware/display controls have no verified factory-default provider.
  // Their own explicit transactions remain separate; never invent values.
}

// The leave guard: true when the page consumed the intent (a page layer is
// up, a picker menu is open or drafts are pending); false when the page is
// clean and the caller navigates.
bool requestLeave() {
  if (!Page || !Api->settings)
    return false;
  // The picker's right-click menu consumes the leave intent like the spray
  // browser layer: ESC/return closes the menu first, never the page.
  if (ModelContextOpen) {
    closeModelContextMenu();
    return true;
  }
  // A picker in flight consumes the leave intent: ESC/return closes the
  // browser, never the whole page (the layer is the top-most surface).
  if (SprayOpen) {
    closeSprayBrowser();
    return true;
  }
  // Any open page layer is the top-most surface: one ESC closes that layer
  // and keeps the page (the same single-layer contract as the picker menu).
  // Without this the menu layer navigates away and the overlay dies with it.
  if (layerVisible()) {
    // The bind-conflict prompt projects from the model's pending flag:
    // hiding the DOM alone would re-show it on the next render, so resolve
    // it as "cancel" exactly like the dialog's own cancel button.
    if (ActiveLayer == kLayerBindConflict && Api->settings)
      Api->settings->bindResolveConflict(0);
    hideLayers();
    render();
    return true;
  }
  if (!isDirty())
    return false;
  showLayer(kLayerLeave);
  return true;
}

void discardAndReturn() {
  if (Api->settings)
    Api->settings->cancel();
  hideLayers();
  render();
  Api->menu->back();
}

// -------------------------------------------------------------- render ----

// The bind sources and the page tree are model data: a document that became
// ready before they were loaded rebuilds its skeleton once the counts settle
// (otherwise the bind list stays empty forever). The counts only move on the
// first model load, so the steady-state cost is three integer compares.
void ensureSkeleton() {
  if (!Page || !Api->settings)
    return;
  const int rowsNow = Api->settings->rowCount();
  const int bindsNow = Api->settings->bindRowCount();
  const int pagesNow = pageCount();
  if (rowsNow == BuiltRowCount && bindsNow == BuiltBindCount &&
      pagesNow == BuiltPageCount)
    return;
  buildRows();
  buildBindRows();
  buildNavigation();
  BuiltRowCount = rowsNow;
  BuiltBindCount = bindsNow;
  BuiltPageCount = pagesNow;
}

void render(const char *skipRow) {
  if (!Page || InRender)
    return;
  InRender = true;
  if (Api->settings) {
    ensureSkeleton();
    const int32_t count = Api->settings->rowCount();
    for (int32_t index = 0; index < count; ++index) {
      AbcUiSettingRow row{};
      if (!rowById(index, row))
        continue;
      renderRow(row, skipRow && std::string_view(row.cvar) == skipRow);
    }
    const int32_t bindCount = Api->settings->bindRowCount();
    for (int32_t index = 0; index < bindCount; ++index) {
      AbcUiBindAction row{};
      row.structSize = sizeof(row);
      if (Api->settings->bindRowAt(index, &row) && !row.header)
        renderBindRow(row);
    }
    renderBindConflict();
  }
  // Always projected: with no model the sections render their fail-closed
  // state instead of staying silently empty.
  renderVideoSection();
  renderVoiceSectionImpl();
  renderPreviews();
  renderCrosshairPreview();
  renderSprayMaker();
  renderModelPicker();
  renderColorRows();
  setStatusSummary();
  setBindStatusSummary();
  InRender = false;
  RenderedRevision = Api->settings ? Api->settings->revision() : 0;
}

// ------------------------------------------------------------ lifecycle ----

void onDocumentReady(void *) {
  if (!Page)
    return;
  Root = Api->documents->root(Page);
  if (Root) {
    ClickBinding =
        Api->ui->elementAddListener(Root, "click", &onRootClick, nullptr);
    ChangeBinding =
        Api->ui->elementAddListener(Root, "change", &onRootChange, nullptr);
    DblClickBinding =
        Api->ui->elementAddListener(Root, "dblclick", &onRootDblClick, nullptr);
    MouseDownBinding = Api->ui->elementAddListener(Root, "mousedown",
                                                   &onRootMouseDown, nullptr);
  }
  // Rows first: the navigation projection moves each widget block into the
  // card of the section that declares it, and those cards exist only after
  // the rows are built.
  buildRows();
  buildBindRows();
  buildNavigation();
  BuiltPageCount = pageCount();
  BuiltRowCount = Api->settings ? Api->settings->rowCount() : 0;
  BuiltBindCount = Api->settings ? Api->settings->bindRowCount() : 0;
  // The legacy all-in-one projection has no cards to claim the widgets: show
  // the parking host so the bind list and the hardware blocks stay reachable.
  if (pageCount() == 0)
    setDisplay("settings-widget-host", true);
}

void onDocumentHide(void *) {
  Presented = false;
  hideLayers();
  closeModelContextMenu();
}

void onDocumentClose(void *) {
  // The DOM is still alive: drop the listeners before it goes.
  if (Root) {
    if (ClickBinding)
      Api->ui->elementRemoveListener(Root, "click", ClickBinding);
    if (ChangeBinding)
      Api->ui->elementRemoveListener(Root, "change", ChangeBinding);
    if (DblClickBinding)
      Api->ui->elementRemoveListener(Root, "dblclick", DblClickBinding);
    if (MouseDownBinding)
      Api->ui->elementRemoveListener(Root, "mousedown", MouseDownBinding);
  }
  ClickBinding = 0;
  ChangeBinding = 0;
  DblClickBinding = 0;
  MouseDownBinding = 0;
  Root = nullptr;
  Built = false;
  Presented = false;
  ActiveLayer.clear();
  SelectShown.clear();
  HueShown.clear();
  VideoOptionsShown.clear();
  ModelBuiltSignature.clear();
  ModelBuiltFavoriteGeneration = 0;
  ModelContextOpen = false;
  ModelContextModel.clear();
  SprayOpen = false;
  SpraySelected.clear();
  VoiceVolumeSkip = nullptr;
  RenderedRevision = ~0u;
}

void load() {
  StateSubscription = Api->events->subscribe(ABCUI_CHANNEL_MENU_STATE, 0,
                                             &onMenuState, nullptr);
  PaintSubscription = Api->events->subscribe(ABCUI_CHANNEL_MENU_PAINT, 0,
                                             &onMenuPaint, nullptr);
  EscapeSubscription = Api->events->subscribeVeto(ABCUI_CHANNEL_SETTINGS_ESCAPE,
                                                  0, &onEscape, nullptr);

  AbcUiDocumentDesc desc{};
  desc.structSize = sizeof(desc);
  desc.name = kDocumentName;
  desc.path = kDocumentPath;
  desc.flags = ABCUI_DOC_FLAG_MENU | ABCUI_DOC_FLAG_BACKDROP;
  desc.zIndex = 0;
  desc.userdata = nullptr;
  desc.onReady = &onDocumentReady;
  desc.onShow = nullptr;
  desc.onHide = &onDocumentHide;
  desc.onClose = &onDocumentClose;
  Page = Api->documents->create(&desc);

  AbcUiDocumentDesc confirm{};
  confirm.structSize = sizeof(confirm);
  confirm.name = kConfirmName;
  confirm.path = kConfirmPath;
  // The confirmation outranks every menu surface and survives a menu hide (a
  // dismissal cannot silently abandon an unconfirmed mode), so it carries
  // neither the menu flag nor a menu tier.
  confirm.flags = ABCUI_DOC_FLAG_BACKDROP;
  confirm.zIndex = ABCUI_DOC_ZINDEX_CONFIRM;
  confirm.userdata = nullptr;
  confirm.onReady = &onConfirmReady;
  confirm.onShow = nullptr;
  confirm.onHide = nullptr;
  confirm.onClose = &onConfirmClose;
  Confirm = Api->documents->create(&confirm);
}

void shutdown() {
  // The documents die with the context; forget every borrowed handle.
  Page = nullptr;
  Confirm = nullptr;
  Root = nullptr;
  ConfirmRoot = nullptr;
  ClickBinding = 0;
  ChangeBinding = 0;
  DblClickBinding = 0;
  MouseDownBinding = 0;
  ConfirmClickBinding = 0;
  Built = false;
  Presented = false;
  ActiveLayer.clear();
  ActivePage.clear();
  SelectShown.clear();
  HueShown.clear();
  VideoOptionsShown.clear();
  ModelBuiltSignature.clear();
  ModelBuiltFavoriteGeneration = 0;
  ModelContextOpen = false;
  ModelContextModel.clear();
  SprayOpen = false;
  SpraySelected.clear();
  VoiceVolumeSkip = nullptr;
  RenderedRevision = ~0u;
}

void unload() {
  if (StateSubscription) {
    Api->events->unsubscribe(StateSubscription);
    StateSubscription = 0;
  }
  if (PaintSubscription) {
    Api->events->unsubscribe(PaintSubscription);
    PaintSubscription = 0;
  }
  if (EscapeSubscription) {
    Api->events->unsubscribe(EscapeSubscription);
    EscapeSubscription = 0;
  }
  Page = nullptr;
  Confirm = nullptr;
}

} // namespace settingspage
