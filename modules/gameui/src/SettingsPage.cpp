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
#include "PageShared.h"
#include "SettingsFileBrowser.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace settingspage {
namespace {

// ---------------------------------------------------------------- state ----

constexpr const char *kDocumentName = "menusettings";
constexpr const char *kDocumentPath =
    "mod://abcui.gameui/gameui/rml/settings.rml";
constexpr const char *kConfirmName = "videoconfirm";
constexpr const char *kConfirmPath =
    "mod://abcui.gameui/gameui/rml/videoconfirm.rml";

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

// Option sets last projected into the select rows (cvar -> values): the
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
struct PreviewPart {
  std::string Color;
  float X = 0;
  float Y = 0;
  float Width = 0;
  float Height = 0;
  bool Valid = false;
};
PreviewPart PreviewCore[5];
PreviewPart PreviewOutline[5];

// ------------------------------------------------------------- constants --

constexpr const char *kActionApply = "apply";
constexpr const char *kActionReset = "reset-defaults";
constexpr const char *kActionResetYes = "reset-yes";
constexpr const char *kActionResetNo = "reset-no";
constexpr const char *kActionReturn = "return";
constexpr const char *kActionLeaveDiscard = "leave-discard";
constexpr const char *kActionLeaveStay = "leave-stay";
constexpr const char *kActionToggle = "toggle";
constexpr const char *kActionAdopt = "adopt";
constexpr const char *kActionBindCapture = "bind-capture";
constexpr const char *kActionBindClear = "bind-clear";
constexpr const char *kActionBindRemove = "bind-remove";
constexpr const char *kActionBindReset = "bind-reset-defaults";
constexpr const char *kActionBindResetYes = "bind-reset-yes";
constexpr const char *kActionBindResetNo = "bind-reset-no";
constexpr const char *kActionBindConflictYes = "bind-conflict-yes";
constexpr const char *kActionBindConflictNo = "bind-conflict-no";
constexpr const char *kActionVideoApply = "video-apply";
constexpr const char *kActionVideoWindowed = "video-toggle-windowed";
constexpr const char *kActionVoiceTest = "voice-test";
constexpr const char *kActionVoiceBoost = "voice-mic-boost";
constexpr const char *kActionSprayChoose = "spray-choose";
constexpr const char *kActionSprayOpen = "spray-file-open";
constexpr const char *kActionSprayUp = "spray-file-up";
constexpr const char *kActionSprayRoot = "spray-file-root";
constexpr const char *kActionSprayConfirm = "spray-file-confirm";
constexpr const char *kActionSprayCancel = "spray-file-cancel";
constexpr const char *kActionModelPick = "model-picker-pick";
constexpr const char *kActionModelFav = "model-picker-fav";
constexpr const char *kActionModelFavOnly = "model-picker-favonly";
constexpr const char *kActionModelContextFav = "model-picker-context-fav";
constexpr const char *kLayerReset = "reset-confirm";
constexpr const char *kLayerLeave = "leave-confirm";
constexpr const char *kLayerBindConflict = "bind-conflict";
constexpr const char *kLayerBindReset = "bind-reset-confirm";
constexpr const char *kLayerSprayFile = "spray-file";

// The section components that own one singleton widget block in settings.rml:
// component name -> block element id. Each claimed block is moved into the
// card of the section that declares it; an unclaimed block waits hidden in
// the widget host. user-script is a page behaviour and has no block.
struct WidgetBlock {
  const char *Component;
  const char *Element;
  bool Preview;
  bool RowFlow;
};

constexpr WidgetBlock kWidgetBlocks[] = {
    {"model-preview", "widget-model-preview", true, false},
    {"model-picker", "widget-model-picker", false, true},
    {"spray-preview", "widget-spray-preview", true, false},
    {"crosshair-preview", "widget-crosshair-preview", true, false},
    {"crosshair-color", "widget-crosshair-color", false, false},
    {"spray-maker", "widget-spray-maker", true, false},
    {"bindings", "widget-bindings", false, false},
    {"video", "widget-video", false, false},
    {"voice", "widget-voice", false, false},
};
constexpr int kWidgetBlockCount =
    static_cast<int>(sizeof(kWidgetBlocks) / sizeof(kWidgetBlocks[0]));

constexpr const char *kColorPickerComponent = "crosshair-color";

// One colour picker element and the four cvar rows it edits.
struct ColorBinding {
  const char *Element;
  const char *R;
  const char *G;
  const char *B;
  const char *A;
};

constexpr ColorBinding kColorBindings[] = {
    {"crosshair-color", "cl_crosshaircolor_r", "cl_crosshaircolor_g",
     "cl_crosshaircolor_b", "cl_crosshairalpha"},
    {"crosshair-outline-color", "cl_crosshaircolor_outline_r",
     "cl_crosshaircolor_outline_g", "cl_crosshaircolor_outline_b",
     "cl_crosshair_outline_alpha"},
};
constexpr int kColorBindingCount =
    static_cast<int>(sizeof(kColorBindings) / sizeof(kColorBindings[0]));

// The kText byte budget (the shipped NameEntry maxchars=63).
constexpr int kTextMaxBytes = 63;

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
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
  return buffer;
}

bool isOn(float value) {
  return value > 0.5f;
}

bool parseNumber(const char *text, float &out) {
  if (!text || !*text)
    return false;
  char *end = nullptr;
  const double parsed = std::strtod(text, &end);
  if (end != text + std::strlen(text) || !std::isfinite(parsed))
    return false;
  out = static_cast<float>(parsed);
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

// The engine stores cl_logocolor as a localized token ("#Valve_Ltblue") while
// the shipped option table uses bare names ("Ltblue"); both spellings select
// the same spray colour, so labels and the preview URI normalize to the bare
// name. Other values pass through unchanged.
std::string logoColorName(const std::string &value) {
  std::string name = value;
  if (!name.empty() && name[0] == '#')
    name.erase(0, 1);
  constexpr const char *kValvePrefix = "Valve_";
  if (name.rfind(kValvePrefix, 0) == 0)
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
  if (std::strcmp(id, kLayerReset) == 0)
    return "reset-no";
  if (std::strcmp(id, kLayerBindConflict) == 0)
    return "bind-conflict-no";
  if (std::strcmp(id, kLayerBindReset) == 0)
    return "bind-reset-no";
  if (std::strcmp(id, kLayerSprayFile) == 0)
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

void closeSprayBrowser();
void renderSprayBrowser();

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

void renderVoiceSection();
void projectWidgets();
void renderRow(AbcUiSettingRow &row, bool skipInput);
void renderBindRow(const AbcUiBindAction &row);
void renderBindConflict();
void renderVideoSection();
void renderVoiceSectionImpl();
void renderPreviews();
void renderCrosshairPreview();
void renderSprayMaker();
void renderModelPicker();
void renderColorRows();
void renderSprayBrowser();
void applyAndReport();
void setStatusSummary();
void setBindStatusSummary();
void closeModelContextMenu();
void render(const char *skipRow = nullptr);

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
  setText("title",
          havePage ? resolve(current.label) : resolve("#Settings_Title"));
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

void AppendRowRow(const AbcUiSettingRow &row, std::string &markup) {
  const std::string cvar = row.cvar;
  markup += "<div class=\"row\" id=\"row-" + cvar +
            "\"><div class=\"row-label\" id=\"row-" + cvar + "-label\"></div>";
  if (isToggleKind(row.kind)) {
    markup += "<button class=\"row-toggle\" id=\"row-" + cvar +
              "-control\" data-action=\"" + kActionToggle + "\" data-row=\"" +
              escapeAttribute(cvar) + "\"></button>";
  } else if (isSelectKind(row.kind)) {
    // Options transfer through the widget Add() at render time (the
    // inline-option transfer is a next-Update delay); the skeleton only
    // carries the empty select.
    markup += "<select class=\"row-select\" id=\"row-" + cvar +
              "-control\" data-row=\"" + escapeAttribute(cvar) + "\"></select>";
  } else {
    const bool slider = row.kind == ABCUI_SET_KIND_FLOAT &&
                        (row.flags & ABCUI_SET_ROW_BOUNDED) &&
                        row.minValue < row.maxValue;
    markup += "<input class=\"row-input";
    // Hue rows carry the drag thumb's own colour swatch (the style sheet
    // swaps their hover/focus feedback onto the thumb border).
    if (row.flags & ABCUI_SET_ROW_HUE_SWATCH)
      markup += " row-hue";
    if (slider)
      markup +=
          " row-slider\" type=\"range\" min=\"" + numberText(row.minValue) +
          "\" max=\"" + numberText(row.maxValue) + "\" step=\"" +
          numberText(row.step > 0 ? row.step
                                  : (row.maxValue - row.minValue) / 1000) +
          "\" value=\"" + numberText(row.draftValue);
    else
      markup += "\" type=\"text";
    markup += "\" id=\"row-" + cvar + "-control\" data-row=\"" +
              escapeAttribute(cvar) + "\"/>";
  }
  markup += "<span class=\"row-value\" id=\"row-" + cvar +
            "-value\"></span><div class=\"row-status\" id=\"row-" + cvar +
            "-status\"></div></div>";
}

void projectWidgets() {
  if (!Page)
    return;
  AbcUiElement *host = node("settings-widget-host");
  if (!host)
    return;
  const int page = pageIndexOf(ActivePage);
  for (int i = 0; i < kWidgetBlockCount; ++i) {
    AbcUiElement *block = node(kWidgetBlocks[i].Element);
    if (!block)
      continue;
    AbcUiElement *target = host;
    if (page >= 0) {
      AbcUiSettingsPage definition{};
      if (pageAt(page, definition)) {
        for (int32_t section = 0; section < definition.sectionCount;
             ++section) {
          if (!sectionHasComponent(page, section, kWidgetBlocks[i].Component))
            continue;
          AbcUiSettingsSection value{};
          if (!sectionAt(page, section, value))
            break;
          const std::string suffix =
              kWidgetBlocks[i].RowFlow ? "-items" : "-widgets";
          if (AbcUiElement *container =
                  node("settings-card-" + std::string(definition.id) + "-" +
                       std::string(value.id) + suffix))
            target = container;
          break;
        }
      }
    }
    if (Api->ui->elementParent(block) == target)
      continue;
    Api->ui->elementAppendChild(target, block);
  }
}

void OpenCard(std::string &markup, const std::string &cardId,
              const std::string &titleToken, bool hasPreview, bool hasBlock,
              bool stacked, bool twoUp) {
  markup += "<div class=\"settings-card";
  if (hasPreview)
    markup += " has-preview";
  if (hasBlock && !hasPreview)
    markup += " has-block";
  if (stacked)
    markup += " has-side-stack";
  if (twoUp)
    markup += " layout-two-up";
  markup += "\" id=\"" + cardId + "\">";
  if (!titleToken.empty())
    markup += "<div class=\"settings-card-title section-header\">" +
              escapeText(resolve(titleToken.c_str())) + "</div>";
  if (hasPreview) {
    markup += "<div class=\"settings-card-body\"><div "
              "class=\"settings-card-items\" id=\"" +
              cardId + "-items\">";
  } else {
    markup += "<div class=\"settings-card-widgets\" id=\"" + cardId +
              "-widgets\"></div>";
    markup += "<div class=\"settings-card-items\" id=\"" + cardId + "-items\">";
  }
}

void CloseCard(std::string &markup, const std::string &cardId,
               bool hasPreview) {
  markup += "</div>";
  if (hasPreview)
    markup += "<div class=\"settings-card-widgets\" id=\"" + cardId +
              "-widgets\"></div></div>";
  markup += "</div>";
}

void buildRows() {
  if (!Page || !Api->settings)
    return;
  AbcUiElement *rows = node("rows");
  if (!rows)
    return;
  std::string markup;
  if (pageCount() == 0) {
    std::string lastSection;
    bool open = false;
    int cardIndex = 0;
    const int count = Api->settings->rowCount();
    for (int index = 0; index < count; ++index) {
      AbcUiSettingRow row{};
      if (!rowById(index, row))
        continue;
      if (row.section[0] && lastSection != row.section) {
        lastSection = row.section;
        if (open)
          CloseCard(markup, "", false);
        OpenCard(markup, "settings-card-legacy-" + std::to_string(cardIndex++),
                 lastSection, false, false, false, false);
        open = true;
      } else if (!open) {
        OpenCard(markup, "settings-card-legacy-" + std::to_string(cardIndex++),
                 "", false, false, false, false);
        open = true;
      }
      AppendRowRow(row, markup);
    }
    if (open)
      CloseCard(markup, "", false);
  } else {
    const int rowCount = Api->settings->rowCount();
    for (int32_t page = 0; page < pageCount(); ++page) {
      AbcUiSettingsPage definition{};
      if (!pageAt(page, definition))
        continue;
      std::vector<std::string> cvars;
      cvars.reserve(static_cast<std::size_t>(definition.cvarCount));
      for (int32_t index = 0; index < definition.cvarCount; ++index) {
        const std::string cvar = pageCvar(page, index);
        if (!cvar.empty())
          cvars.push_back(cvar);
      }
      markup += "<div class=\"settings-cvar-page\" id=\"settings-rows-" +
                std::string(definition.id) + "\">";
      for (int32_t section = 0; section < definition.sectionCount; ++section) {
        AbcUiSettingsSection declared{};
        if (!sectionAt(page, section, declared))
          continue;
        bool hasPreview = false;
        bool hasBlock = false;
        bool hasWidget = false;
        for (int32_t component = 0; component < declared.componentCount;
             ++component) {
          const std::string name = sectionComponent(page, section, component);
          for (int i = 0; i < kWidgetBlockCount; ++i) {
            if (name != kWidgetBlocks[i].Component)
              continue;
            hasWidget = true;
            if (kWidgetBlocks[i].Preview)
              hasPreview = true;
            else if (!kWidgetBlocks[i].RowFlow)
              hasBlock = true;
          }
        }
        const auto visibleRow = [&](const AbcUiSettingRow &row) {
          if (declared.id[0] && std::strcmp(row.section, declared.id) != 0)
            return false;
          if (std::find(cvars.begin(), cvars.end(), row.cvar) == cvars.end())
            return false;
          if (sectionHasComponent(page, section, "model-picker") &&
              std::strcmp(row.cvar, "model") == 0)
            return false;
          if (sectionHasComponent(page, section, kColorPickerComponent) &&
              ownsColorCvar(row.cvar))
            return false;
          return true;
        };
        int count = 0;
        for (int index = 0; index < rowCount; ++index) {
          AbcUiSettingRow row{};
          if (rowById(index, row) && visibleRow(row))
            ++count;
        }
        if (count == 0 && !hasWidget)
          continue;
        const bool twoUp = std::strcmp(declared.layout, "two-up") == 0;
        const bool stacked = !twoUp && hasPreview && hasBlock;
        const std::string cardId = "settings-card-" +
                                   std::string(definition.id) + "-" +
                                   std::string(declared.id);
        OpenCard(markup, cardId, declared.label, twoUp ? false : hasPreview,
                 twoUp ? false : hasBlock, stacked, twoUp);
        if (twoUp) {
          int inPair = 0;
          for (int index = 0; index < rowCount; ++index) {
            AbcUiSettingRow row{};
            if (!rowById(index, row) || !visibleRow(row))
              continue;
            if (inPair == 0)
              markup += "<div class=\"row-pair\">";
            AppendRowRow(row, markup);
            if (++inPair == 2) {
              markup += "</div>";
              inPair = 0;
            }
          }
          if (inPair != 0)
            markup += "</div>";
        } else {
          for (int index = 0; index < rowCount; ++index) {
            AbcUiSettingRow row{};
            if (rowById(index, row) && visibleRow(row))
              AppendRowRow(row, markup);
          }
        }
        CloseCard(markup, cardId, twoUp ? false : hasPreview);
      }
      markup += "</div>";
    }
  }
  Api->ui->elementSetInnerRML(rows, markup.c_str());
  Built = true;
}

void renderRow(AbcUiSettingRow &row, bool skipInput) {
  if (!Page)
    return;
  const std::string cvar = row.cvar;
  AbcUiElement *rowNode = node("row-" + cvar);
  if (!rowNode)
    return;
  setInnerRml(node("row-" + cvar + "-label"),
              escapeText(resolve(row.labelToken)));

  const bool toggle = isToggleKind(row.kind);
  const bool select = isSelectKind(row.kind);
  AbcUiElement *control = node("row-" + cvar + "-control");
  if (!control)
    return;
  const bool missing = (row.flags & ABCUI_SET_ROW_MISSING) != 0;
  const bool editable = (row.flags & ABCUI_SET_ROW_EDITABLE) != 0;
  Api->ui->elementSetClass(control, "row-missing", missing);
  Api->ui->elementSetClass(control, "row-readonly",
                           (row.flags & ABCUI_SET_ROW_READ_ONLY) != 0);
  Api->ui->elementSetDisabled(control, missing || !editable);

  if (missing) {
    // Unavailable: fail-closed presentation, no editable control path.
    const std::string text = row.requireCvar[0]
                                 ? resolve("#Settings_DependencyUnavailable") +
                                       " " + std::string(row.requireCvar)
                                 : resolve("#Settings_Unavailable");
    setInnerRml(node("row-" + cvar + "-status"), escapeText(text));
    return;
  }

  if (AbcUiElement *value = node("row-" + cvar + "-value")) {
    // Only the bounded float rows carry a readout; an empty inline block
    // would wrap onto its own line inside the narrow card column and push
    // the status line away from its control.
    const bool shows =
        row.kind == ABCUI_SET_KIND_FLOAT && (row.flags & ABCUI_SET_ROW_BOUNDED);
    setProperty(value, "display", shows ? "inline-block" : "none");
    setInnerRml(value, shows ? escapeText(row.draftText) : std::string());
  }

  // Hue rows (topcolor / bottomcolor): the drag thumb shows the colour the
  // value represents. The engine reads 0..255 as palette-hue degrees, so the
  // same mapping paints the swatch; the write is skipped while the colour is
  // unchanged (a render per input event must not re-dirty the style tree).
  if ((row.flags & ABCUI_SET_ROW_HUE_SWATCH) &&
      row.kind == ABCUI_SET_KIND_FLOAT) {
    if (AbcUiElement *thumb = Api->ui->elementFindTag(control, "sliderbar")) {
      char swatch[32];
      Api->ui->formatHueColor(row.draftValue, swatch, sizeof(swatch));
      std::string stored;
      bool known = false;
      for (const auto &entry : HueShown) {
        if (entry.first != cvar)
          continue;
        stored = entry.second;
        known = true;
        break;
      }
      if (!known || stored != swatch) {
        setProperty(thumb, "background-color", swatch);
        bool replaced = false;
        for (auto &entry : HueShown) {
          if (entry.first != cvar)
            continue;
          entry.second = swatch;
          replaced = true;
          break;
        }
        if (!replaced)
          HueShown.emplace_back(cvar, swatch);
      }
    }
  }

  if (toggle) {
    const bool on =
        row.state == ABCUI_SET_STATE_INVALID
            ? (row.kind == ABCUI_SET_KIND_NEGATE ? row.engineValue < 0.0f
                                                 : isOn(row.engineValue))
            : toggleDraftOn(row);
    setInnerRml(control,
                escapeText(resolve(on ? "#Settings_On" : "#Settings_Off")));
  } else if (select) {
    // Options transfer through the widget's Add() (inline RML children only
    // become matchable on the next Update). The set is rebuilt only when it
    // changed, and the live engine value is always offerable (a downloaded
    // model must stay selectable even if the scan missed it).
    std::vector<std::string> values;
    std::vector<std::string> labels;
    // Invalid enum text cannot be shown by a select; fall back to the engine
    // value so the widget keeps an honest selection.
    const std::string shownText =
        row.state == ABCUI_SET_STATE_INVALID
            ? (row.kind == ABCUI_SET_KIND_ENUM ? numberText(row.engineValue)
                                               : std::string(row.engineText))
            : std::string(row.draftText);
    // Empty values are legal options (the "unset" placeholder): the widget
    // auto-selects the first option when nothing matches, and a skipped
    // placeholder would make it commit a real option instead.
    const auto addOption = [&values, &labels](const std::string &value,
                                              const std::string &label) {
      for (const auto &existing : values)
        if (existing == value)
          return;
      values.push_back(value);
      labels.push_back(label);
    };
    if (row.kind == ABCUI_SET_KIND_ENUM) {
      // The current/staged value is always offerable FIRST: the localized
      // scripts ship option sets that can omit the user's live value, and
      // without the live entry the widget auto-picks the first listed option
      // and commits it as a draft.
      std::string shownLabel = shownText;
      for (int32_t index = 0; index < row.optionCount; ++index) {
        float optionValue = 0.0f;
        char label[ABCUI_SETTINGS_LABEL_MAX];
        label[0] = '\0';
        if (!Api->settings->optionAt(cvar.c_str(), index, &optionValue, label,
                                     sizeof(label)))
          continue;
        if (numberText(optionValue) == shownText)
          shownLabel = resolve(label);
      }
      addOption(shownText, shownLabel);
      for (int32_t index = 0; index < row.optionCount; ++index) {
        float optionValue = 0.0f;
        char label[ABCUI_SETTINGS_LABEL_MAX];
        label[0] = '\0';
        if (!Api->settings->optionAt(cvar.c_str(), index, &optionValue, label,
                                     sizeof(label)))
          continue;
        addOption(numberText(optionValue), resolve(label));
      }
    } else {
      const auto stringLabel = [&row](const std::string &value) -> std::string {
        if (std::strcmp(row.stateName, "logocolor") == 0)
          return resolve(("#Valve_" + logoColorName(value)).c_str());
        if (row.flags & ABCUI_SET_ROW_FILE_CHOICES)
          return fileStem(value);
        return value;
      };
      // The live value rides FIRST (the widget auto-picks the first option
      // when it is added, and the deferred change it fires then carries the
      // live value = clean). An empty live value gets an "unset" placeholder
      // so the widget still shows something honest.
      if (shownText.empty())
        addOption("", resolve("#Settings_Unset"));
      else
        addOption(shownText, stringLabel(shownText));
      for (int32_t index = 0; index < row.optionCount; ++index) {
        float optionValue = 0.0f;
        char label[ABCUI_SETTINGS_LABEL_MAX];
        label[0] = '\0';
        if (!Api->settings->optionAt(cvar.c_str(), index, &optionValue, label,
                                     sizeof(label)))
          continue;
        addOption(label, stringLabel(label));
      }
      for (int32_t index = 0; index < row.choiceCount; ++index) {
        char value[ABCUI_SETTINGS_TEXT_MAX];
        char label[ABCUI_SETTINGS_LABEL_MAX];
        value[0] = '\0';
        label[0] = '\0';
        if (!Api->settings->choiceAt(cvar.c_str(), index, value, sizeof(value),
                                     label, sizeof(label)))
          continue;
        addOption(value, label[0] ? std::string(label) : stringLabel(value));
      }
    }
    std::vector<std::string> *shown = nullptr;
    for (auto &entry : SelectShown) {
      if (entry.first != cvar)
        continue;
      shown = &entry.second;
      break;
    }
    if (!shown || *shown != values) {
      Api->ui->elementSelectRemoveAll(control);
      for (std::size_t index = 0; index < values.size(); ++index) {
        const std::string label = escapeText(labels[index]);
        Api->ui->elementSelectAdd(control, label.c_str(),
                                  values[index].c_str());
      }
      if (shown)
        *shown = values;
      else
        SelectShown.emplace_back(cvar, values);
    }
    // Selection through SetSelection (a programmatic value attribute WOULD
    // dispatch change - the stack-overflow trap).
    int32_t selection = -1;
    for (std::size_t index = 0; index < values.size(); ++index) {
      if (values[index] == shownText) {
        selection = static_cast<int32_t>(index);
        break;
      }
    }
    if (selection >= 0 && Api->ui->elementSelectSelection(control) != selection)
      Api->ui->elementSelectSetSelection(control, selection);
  } else if (!skipInput) {
    // The committed text is the projection; a programmatic value write does
    // not dispatch change (no feedback loop). The origin field of a change
    // commit is skipped entirely so its caret and IME state survive.
    Api->ui->elementSetValue(control, row.draftText);
  }

  // Row status: domain facts plus the pending-state suffix.
  std::string status;
  switch (row.kind) {
  case ABCUI_SET_KIND_FLOAT:
    status =
        (row.flags & ABCUI_SET_ROW_BOUNDED)
            ? numberText(row.minValue) + "-" + numberText(row.maxValue) + " " +
                  resolve("#Settings_Default") + " " +
                  numberText(row.defaultValue)
            : resolve("#Settings_Default") + " " + numberText(row.defaultValue);
    break;
  case ABCUI_SET_KIND_ENUM:
    status = std::to_string(row.optionCount) + " " +
             resolve("#Settings_Choices") + " | " +
             resolve("#Settings_Default") + " " + numberText(row.defaultValue);
    break;
  case ABCUI_SET_KIND_TEXT:
    status = std::to_string(kTextMaxBytes) + " " + resolve("#Settings_TextMax");
    break;
  case ABCUI_SET_KIND_NEGATE:
    status = resolve("#Settings_NegateDomain");
    break;
  case ABCUI_SET_KIND_STRING_ENUM:
    status = resolve("#Settings_EnumDomain");
    break;
  case ABCUI_SET_KIND_COMMAND_TOGGLE:
    status = resolve("#Settings_CommandDomain");
    break;
  case ABCUI_SET_KIND_BOOL:
    status = resolve("#Settings_BoolDomain");
    break;
  }
  if (!(row.flags & ABCUI_SET_ROW_HAS_DEFAULT))
    status = resolve("#Settings_NoDefault");
  switch (row.state) {
  case ABCUI_SET_STATE_DIRTY:
    status += " | " + resolve("#Settings_Dirty");
    break;
  case ABCUI_SET_STATE_INVALID:
    // The typed text is user input: it rides the status string and the
    // single escape below makes it inert (no inner escaping here - that
    // would double-encode).
    status += " | " + resolve("#Settings_Invalid") + " (" +
              std::string(row.draftText) + ")";
    break;
  case ABCUI_SET_STATE_CONFLICT:
    status += " | " + resolve("#Settings_Conflict") + " " +
              (row.kind == ABCUI_SET_KIND_TEXT ||
                       row.kind == ABCUI_SET_KIND_STRING_ENUM
                   ? std::string(row.engineText)
                   : numberText(row.engineValue));
    break;
  case ABCUI_SET_STATE_QUEUED:
    status += " | " + resolve("#Settings_Queued");
    break;
  case ABCUI_SET_STATE_APPLY_FAILED:
    status += " | " + resolve("#Settings_ApplyFailed");
    break;
  case ABCUI_SET_STATE_CLEAN:
  case ABCUI_SET_STATE_MISSING:
    break;
  }
  if (row.flags & ABCUI_SET_ROW_READ_ONLY)
    status = resolve("#Settings_ReadOnly");
  setInnerRml(node("row-" + cvar + "-status"), escapeText(status));
}

void setStatusSummary() {
  if (!Page || !Api->settings)
    return;
  std::string text;
  if (!Api->settings->dirty()) {
    text = resolve("#Settings_StatusClean");
  } else {
    text = resolve("#Settings_StatusDirty");
    if (Api->settings->hasConflicts())
      text += " | " + resolve("#Settings_StatusConflict");
  }
  setInnerRml(node("settings-status"), escapeText(text));
}

// ------------------------------------------------------- crosshair colors --

bool parseHex(const char *text, unsigned char out[4]) {
  if (!text)
    return false;
  const char *digits = text;
  if (*digits == '#')
    ++digits;
  const std::size_t length = std::strlen(digits);
  if (length != 6 && length != 8)
    return false;
  unsigned int value[4] = {0, 0, 0, 255};
  for (std::size_t i = 0; i < length / 2; ++i) {
    unsigned int byte = 0;
    for (int half = 0; half < 2; ++half) {
      const char c = digits[i * 2 + half];
      unsigned int nibble = 0;
      if (c >= '0' && c <= '9')
        nibble = static_cast<unsigned int>(c - '0');
      else if (c >= 'a' && c <= 'f')
        nibble = static_cast<unsigned int>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        nibble = static_cast<unsigned int>(c - 'A' + 10);
      else
        return false;
      byte = byte * 16 + nibble;
    }
    value[i] = byte;
  }
  for (int i = 0; i < 4; ++i)
    out[i] = static_cast<unsigned char>(value[i]);
  return true;
}

std::string formatHex(const unsigned char color[4]) {
  char buffer[10];
  std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x%02x", color[0], color[1],
                color[2], color[3]);
  return buffer;
}

int channelValue(float value) {
  if (!std::isfinite(value))
    return 0;
  return std::clamp(static_cast<int>(std::lround(value)), 0, 255);
}

const ColorBinding *bindingFor(const char *elementId) {
  if (!elementId)
    return nullptr;
  for (int i = 0; i < kColorBindingCount; ++i)
    if (std::strcmp(elementId, kColorBindings[i].Element) == 0)
      return &kColorBindings[i];
  return nullptr;
}

void renderColorRows() {
  if (!Page || !Api->settings)
    return;
  for (int i = 0; i < kColorBindingCount; ++i) {
    const ColorBinding &binding = kColorBindings[i];
    AbcUiElement *element = node(binding.Element);
    if (!element)
      continue;
    const char *cvars[4] = {binding.R, binding.G, binding.B, binding.A};
    unsigned char color[4] = {0, 0, 0, 255};
    bool missing = false;
    for (int channel = 0; channel < 4; ++channel) {
      AbcUiSettingRow row{};
      if (!rowFound(cvars[channel], row) ||
          (row.flags & ABCUI_SET_ROW_MISSING)) {
        missing = true;
        continue;
      }
      // An invalid draft keeps its text but has no numeric reading; the
      // picker then shows the engine truth instead of a snapped guess.
      const float value = row.state == ABCUI_SET_STATE_INVALID ? row.engineValue
                                                               : row.draftValue;
      color[channel] = static_cast<unsigned char>(channelValue(value));
    }
    const std::string hex = formatHex(color);
    if (attributeOf(element, "value") != hex)
      Api->ui->elementSetAttribute(element, "value", hex.c_str());
    Api->ui->elementSetAttribute(element, "disabled",
                                 missing ? "disabled" : nullptr);
  }
}

void handleColorChange(const char *elementId, const char *value) {
  const ColorBinding *binding = bindingFor(elementId);
  if (!binding)
    return;
  unsigned char color[4];
  if (!parseHex(value, color) || !Api->settings)
    return; // picker intent with an unusable payload: consumed, no-op
  const char *cvars[4] = {binding->R, binding->G, binding->B, binding->A};
  for (int channel = 0; channel < 4; ++channel) {
    AbcUiSettingRow row{};
    if (!rowFound(cvars[channel], row) || (row.flags & ABCUI_SET_ROW_MISSING))
      return; // fail-closed: the element is disabled in this state
    char text[8];
    std::snprintf(text, sizeof(text), "%d", static_cast<int>(color[channel]));
    Api->settings->setDraftText(cvars[channel], text);
  }
}

// ----------------------------------------------------------- bindings -----

// kb_act.lst delimits its sections with "blank" rows whose label is the
// vanilla file's ASCII rule ("==========================="). Those rows are
// file typography, not content: this theme renders the real titles only, so
// the rules (and any label-less divider) are dropped from the skeleton.
bool bindHeaderIsRule(const AbcUiBindAction &row) {
  for (const char *c = row.label; *c; ++c) {
    if (*c != '=' && *c != ' ' && *c != '\t')
      return false;
  }
  return true;
}

// Shared resolution for one raw label string ("#Valve_X" or literal).
std::string bindLabelText(const std::string &label) {
  if (label.empty() || label[0] != '#')
    return label;
  const std::string token = label.substr(1);
  std::string text = translate(token.c_str());
  if (text != token)
    return text;
  char buffer[ABCUI_SETTINGS_LABEL_MAX];
  buffer[0] = '\0';
  if (Api->settings &&
      Api->settings->bindLookupLabel(token.c_str(), buffer, sizeof(buffer)))
    return buffer;
  return label;
}

std::string bindActionLabel(const AbcUiBindAction &row) {
  return bindLabelText(row.label);
}

void buildBindRows() {
  if (!Page || !Api->settings)
    return;
  AbcUiElement *rows = node("bindrows");
  if (!rows)
    return;
  // One skeleton block per kb_act entry: section headers render as plain
  // dividers, actions carry label + key list + bind/clear buttons. The DOM
  // id is the action index (a plain number).
  std::string markup;
  const int32_t count = Api->settings->bindRowCount();
  for (int32_t index = 0; index < count; ++index) {
    AbcUiBindAction row{};
    row.structSize = sizeof(row);
    if (!Api->settings->bindRowAt(index, &row))
      continue;
    if (row.header) {
      if (bindHeaderIsRule(row))
        continue;
      markup += "<div class=\"bind-header\"><span>" +
                escapeText(bindActionLabel(row)) +
                "</span><span class=\"bind-header-columns\">" +
                escapeText(resolve("#Settings_BindColumns")) + "</span></div>";
      continue;
    }
    const std::string id = std::to_string(row.index);
    markup += "<div class=\"bind-row\" id=\"bindrow-" + id +
              "\"><div class=\"bind-line\"><div class=\"bind-label\" "
              "id=\"bindrow-" +
              id + "-label\"></div><div class=\"bind-keys\" id=\"bindrow-" +
              id +
              "-keys\"></div><button class=\"bind-btn bind-capture-btn\" "
              "id=\"bindrow-" +
              id + "-capture\" data-action=\"" + kActionBindCapture +
              "\" data-row=\"" + id +
              "\"></button><button class=\"bind-btn bind-clear-btn\" "
              "id=\"bindrow-" +
              id + "-clear\" data-action=\"" + kActionBindClear +
              "\" data-row=\"" + id +
              "\"></button></div><div "
              "class=\"bind-status\" id=\"bindrow-" +
              id + "-status\"></div></div>";
  }
  Api->ui->elementSetInnerRML(rows, markup.c_str());
}

void renderBindRow(const AbcUiBindAction &row) {
  if (!Page)
    return;
  const std::string id = std::to_string(row.index);
  AbcUiElement *rowNode = node("bindrow-" + id);
  if (!rowNode)
    return;
  // The capturing row highlights its key list (class, not a display
  // property - the color lands immediately).
  const bool capturing = Api->settings->bindCapturing() == row.index;
  Api->ui->elementSetClass(rowNode, "bind-capturing", capturing);
  setInnerRml(node("bindrow-" + id + "-label"),
              escapeText(bindActionLabel(row)));

  // Keys in draft order; "unbound" placeholder keeps the row readable.
  std::string keys;
  for (int32_t index = 0; index < row.keyCount; ++index) {
    char key[ABCUI_SETTINGS_KEY_MAX];
    key[0] = '\0';
    if (!Api->settings->bindKeyAt(row.index, index, key, sizeof(key)))
      continue;
    if (!keys.empty())
      keys += ", ";
    keys += key;
  }
  if (keys.empty())
    keys = resolve("#Settings_BindNone");
  setInnerRml(node("bindrow-" + id + "-keys"), escapeText(keys));

  // Buttons: capture text flips while this row is capturing; clear is the
  // unbind-all-keys affordance of the row (drafts only until Apply).
  setInnerRml(node("bindrow-" + id + "-capture"),
              escapeText(resolve(capturing ? "#Settings_BindCancelCapture"
                                           : "#Settings_BindCapture")));
  setInnerRml(node("bindrow-" + id + "-clear"),
              escapeText(resolve("#Settings_BindClear")));

  std::string status;
  if (capturing) {
    status = resolve("#Settings_BindPressKey");
  } else {
    switch (row.state) {
    case ABCUI_BIND_STATE_DIRTY:
      status = resolve("#Settings_BindDirty");
      break;
    case ABCUI_BIND_STATE_QUEUED:
      status = resolve("#Settings_BindQueued");
      break;
    case ABCUI_BIND_STATE_APPLY_FAILED:
      status = resolve("#Settings_BindFailed");
      break;
    case ABCUI_BIND_STATE_UNAVAILABLE:
      status = resolve("#Settings_BindUnavailable");
      break;
    case ABCUI_BIND_STATE_CLEAN:
      break;
    }
  }
  setInnerRml(node("bindrow-" + id + "-status"), escapeText(status));
}

void renderBindConflict() {
  if (!Page || !Api->settings)
    return;
  AbcUiElement *layer = node(kLayerBindConflict);
  if (!layer)
    return;
  if (Api->settings->bindConflictPending()) {
    // The keyname and the other action's label ride the layer text, each
    // piece escaped on its own (the label may carry user-facing engine
    // strings; nothing here is ever injected unescaped).
    char keyName[ABCUI_SETTINGS_KEY_MAX];
    char otherLabel[ABCUI_SETTINGS_LABEL_MAX];
    keyName[0] = '\0';
    otherLabel[0] = '\0';
    Api->settings->bindConflictKeyName(keyName, sizeof(keyName));
    Api->settings->bindConflictOtherLabel(otherLabel, sizeof(otherLabel));
    setInnerRml(node("bind-conflict-text"),
                escapeText(resolve("#Settings_BindConflictLead")) + " " +
                    escapeText(keyName) + " " +
                    escapeText(resolve("#Settings_BindBoundTo")) + " " +
                    escapeText(bindLabelText(otherLabel)) + "?");
    if (ActiveLayer != kLayerBindConflict)
      showLayer(kLayerBindConflict);
  } else if (ActiveLayer == kLayerBindConflict) {
    hideLayers();
  }
}

void setBindStatusSummary() {
  if (!Page || !Api->settings)
    return;
  std::string text;
  if (!Api->settings->bindReadsAvailable()) {
    text = resolve("#Settings_BindUnavailable");
  } else if (Api->settings->bindConflictPending()) {
    text = resolve("#Settings_BindConflictSummary");
  } else if (!Api->settings->bindDirty()) {
    text = resolve("#Settings_BindClean");
  } else {
    text = resolve("#Settings_BindDirtySummary");
  }
  setInnerRml(node("bind-status"), escapeText(text));
}

// ------------------------------------------------------- display modes ----

std::string describeMode(int32_t width, int32_t height, int32_t windowed) {
  char buffer[ABCUI_SETTINGS_TEXT_MAX];
  buffer[0] = '\0';
  if (!Api->settings || !Api->settings->videoDescribe(width, height, windowed,
                                                      buffer, sizeof(buffer)))
    return {};
  return buffer;
}

void renderVideoSection() {
  if (!Page)
    return;
  AbcUiElement *section = node("video-section");
  if (!section)
    return;
  AbcUiVideoFacts facts{};
  const bool available = videoFacts(facts) && facts.available;
  Api->ui->elementSetClass(section, "video-unavailable", !available);

  AbcUiElement *select = node("video-resolution");
  AbcUiElement *toggle = node("video-windowed");
  AbcUiElement *apply = node("video-apply");
  // RmlUi pin: <button> instantiates as a generic Element (no form-control
  // disabled semantics - only <input>/<select> are form controls), so buttons
  // gate visually through the class while the click handlers keep their own
  // model-state guards. The select is a real form control.
  if (!available) {
    setInnerRml(node("video-status"),
                escapeText(resolve("#Settings_VideoUnavailable")));
    Api->ui->elementSetDisabled(select, 1);
    Api->ui->elementSetClass(toggle, "video-disabled", 1);
    Api->ui->elementSetDisabled(apply, 1);
    Api->ui->elementSetClass(apply, "video-disabled", 1);
    return;
  }
  // The section may have been left locked by an earlier projection: clear
  // the lock before projecting the current state.
  Api->ui->elementSetDisabled(select, 0);

  // The resolution options are built ONLY once the actual mode is known (a
  // projection before the first snapshot would pollute the list with a 0x0
  // entry) and rebuilt only when the set changes. The control's own Add() is
  // used, not inner RML: Add routes through the widget so the option is
  // matchable in the SAME pass.
  std::vector<std::pair<int, int>> options;
  if (facts.actualKnown) {
    options.emplace_back(facts.actualWidth, facts.actualHeight);
    for (int32_t index = 0; index < facts.modeCount; ++index) {
      int32_t width = 0;
      int32_t height = 0;
      if (!Api->settings->videoModeAt(index, &width, &height))
        continue;
      bool known = false;
      for (const auto &existing : options)
        known = known || (existing.first == width && existing.second == height);
      if (!known)
        options.emplace_back(width, height);
    }
  }
  if (select && options != VideoOptionsShown) {
    Api->ui->elementSelectRemoveAll(select);
    for (const auto &option : options) {
      const std::string value =
          std::to_string(option.first) + "x" + std::to_string(option.second);
      const std::string label = escapeText(value);
      Api->ui->elementSelectAdd(select, label.c_str(), value.c_str());
    }
    VideoOptionsShown = options;
  }

  // Projection values: the staged target over the actual mode. Selection goes
  // through SetSelection: it reconciles the widget without dispatching a
  // change event, so no feedback loop is possible.
  const bool stagedResolution = facts.hasStaged != 0;
  const int32_t targetWidth =
      stagedResolution ? facts.stagedWidth : facts.actualWidth;
  const int32_t targetHeight =
      stagedResolution ? facts.stagedHeight : facts.actualHeight;
  const int32_t targetWindowed =
      stagedResolution ? facts.stagedWindowed : facts.actualWindowed;
  if (select && !VideoOptionsShown.empty()) {
    int32_t selection = -1;
    for (std::size_t index = 0; index < VideoOptionsShown.size(); ++index) {
      if (VideoOptionsShown[index].first == targetWidth &&
          VideoOptionsShown[index].second == targetHeight) {
        selection = static_cast<int32_t>(index);
        break;
      }
    }
    if (selection >= 0 && Api->ui->elementSelectSelection(select) != selection)
      Api->ui->elementSelectSetSelection(select, selection);
  }
  if (toggle) {
    setInnerRml(toggle, escapeText(resolve(
                            targetWindowed ? "#Settings_VideoWindowedOn"
                                           : "#Settings_VideoWindowedOff")));
  }
  // While a confirmation is outstanding the whole section is locked (the
  // pending switch owns the engine state); otherwise the controls follow the
  // stage.
  const bool locked = facts.awaitingConfirm != 0;
  const bool canApply = facts.hasStaged != 0 && !locked;
  Api->ui->elementSetDisabled(select, locked);
  Api->ui->elementSetClass(toggle, "video-disabled", locked);
  Api->ui->elementSetDisabled(apply, !canApply);
  Api->ui->elementSetClass(apply, "video-disabled", !canApply);

  std::string text =
      resolve("#Settings_VideoActual") + " " +
      describeMode(facts.actualWidth, facts.actualHeight, facts.actualWindowed);
  text += " | ";
  if (locked) {
    text += resolve("#Settings_VideoAwaiting");
  } else if (facts.hasStaged) {
    text += resolve("#Settings_VideoStaged") + " " +
            describeMode(targetWidth, targetHeight, targetWindowed);
  } else {
    text += resolve("#Settings_VideoNone");
  }
  setInnerRml(node("video-status"), escapeText(text));
}

// ------------------------------------------------ microphone hardware -----

void renderVoiceSectionImpl() {
  if (!Page)
    return;
  AbcUiElement *section = node("voice-section");
  if (!section)
    return;
  AbcUiVoiceFacts facts{};
  const bool available = voiceFacts(facts) && facts.available;
  const float level = available ? facts.speakingLevel : 0.0f;
  if (AbcUiElement *fill = node("voice-meter-fill")) {
    const float clamped = std::clamp(level, 0.0f, 1.0f);
    char width[32];
    std::snprintf(width, sizeof(width), "%g%%", clamped * 100.0f);
    setProperty(fill, "width", width);
    setProperty(fill, "background-color",
                level > 0.85f  ? "#dd634f"
                : level > 0.6f ? "#d4bd4c"
                               : "#68bd66");
  }
  Api->ui->elementSetClass(section, "video-unavailable", !available);

  AbcUiElement *volumeInput = node("voice-mic-volume");
  AbcUiElement *boostToggle = node("voice-mic-boost");
  AbcUiElement *testToggle = node("voice-mic-test");
  AbcUiElement *status = node("voice-status");
  if (!available) {
    setInnerRml(status, escapeText(resolve("#Settings_VoiceUnavailable")));
    Api->ui->elementSetDisabled(volumeInput, 1);
    Api->ui->elementSetClass(boostToggle, "video-disabled", 1);
    Api->ui->elementSetClass(testToggle, "video-disabled", 1);
    return;
  }
  Api->ui->elementSetDisabled(volumeInput, 0);

  // The volume field shows the 0..100 display scale (the original page's
  // GetControlFloat*100 mapping). The origin field of a change commit is
  // skipped like every text row (caret/IME survival).
  if (volumeInput && volumeInput != VoiceVolumeSkip) {
    const float shown = facts.micDirty ? facts.micVolumeDraft : facts.micVolume;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d",
                  static_cast<int>(shown * 100.0f + 0.5f));
    Api->ui->elementSetValue(volumeInput, buffer);
    setInnerRml(node("voice-mic-value"), std::string(buffer) + "%");
  }
  if (boostToggle) {
    const bool on =
        facts.micBoostDirty ? facts.micBoostDraft != 0 : facts.micBoost != 0;
    setInnerRml(boostToggle,
                escapeText(resolve(on ? "#Settings_On" : "#Settings_Off")));
    Api->ui->elementSetClass(boostToggle, "video-disabled", 0);
  }
  if (testToggle) {
    setInnerRml(testToggle, escapeText(resolve(
                                facts.testing ? "#Settings_VoiceStopTest"
                                              : "#Settings_VoiceStartTest")));
    Api->ui->elementSetClass(testToggle, "video-disabled", 0);
  }

  if (status) {
    std::string text;
    if (facts.testing) {
      // The level is the engine's GetSpeakingVolume answer, rendered as a
      // plain number - never an animated bar pretending to be a sample.
      const int percent = static_cast<int>(facts.speakingLevel * 100.0f + 0.5f);
      text =
          resolve("#Settings_VoiceLevel") + " " + std::to_string(percent) + "%";
    } else {
      text = resolve("#Settings_VoiceIdle");
    }
    if (facts.micDirty || facts.micBoostDirty)
      text += " | " + resolve("#Settings_Dirty");
    if (facts.micBoostDirty) {
      text += " | " + resolve("#Settings_MicBoost") + " " +
              resolve(facts.micBoostDraft ? "#Settings_On" : "#Settings_Off");
    }
    setInnerRml(status, escapeText(text));
  }
}

void renderVoiceSection() {
  renderVoiceSectionImpl();
}

// ------------------------------------------------------------ previews -----

void setPreviewSource(const char *id, const std::string &uri) {
  AbcUiElement *image = node(id);
  if (!image)
    return;
  const std::string previous = attributeOf(image, "src");
  if (previous == uri)
    return;
  Api->ui->elementSetAttribute(image, "src", uri.c_str());
  // Rml caches file textures globally. Retire the old palette variant so
  // dragging colors cannot accumulate GPU textures.
  if (!previous.empty())
    Api->ui->releaseTexture(previous.c_str());
}

void renderPreviews() {
  if (!Page || !Api->settings)
    return;
  const auto textOf = [](const char *cvar) -> std::string {
    AbcUiSettingRow row{};
    if (!rowFound(cvar, row) || (row.flags & ABCUI_SET_ROW_MISSING))
      return {};
    return row.draftText;
  };
  // The player model is a LIVE Studio preview (drag to turn it): bind the
  // cvar's model directory to a resolvable <model> source, the same mapping
  // the main menu's character card uses. The player colors are not a preview
  // property of the renderer, so they stay out of the 3D view.
  {
    const std::string model = textOf("model");
    std::string path = "models/player.mdl";
    if (model.size() >= 4 && model.compare(model.size() - 4, 4, ".mdl") == 0)
      path = model;
    else if (!model.empty())
      path = "models/player/" + model + "/" + model + ".mdl";
    if (AbcUiElement *preview = node("model-preview")) {
      if (attributeOf(preview, "src") != path)
        Api->ui->elementSetAttribute(preview, "src", path.c_str());
    }
  }
  // The engine's stored colour token ("#Valve_Ltblue") normalizes to the bare
  // palette name the preview decoder expects.
  setPreviewSource("spray-preview", "preview://spray/" + textOf("cl_logofile") +
                                        "/" +
                                        logoColorName(textOf("cl_logocolor")));
}

// ----------------------------------------------------- crosshair preview ---

constexpr const char *kCoreIds[5] = {"cx-core-center", "cx-core-top",
                                     "cx-core-bottom", "cx-core-left",
                                     "cx-core-right"};
constexpr const char *kOutlineIds[5] = {"cx-outline-center", "cx-outline-top",
                                        "cx-outline-bottom", "cx-outline-left",
                                        "cx-outline-right"};
constexpr const char *kTransparentColor = "rgba(0, 0, 0, 0)";

std::string rgbaText(int red, int green, int blue, int alpha) {
  char buffer[48];
  buffer[0] = '\0';
  Api->ui->formatRgba(red, green, blue, alpha, buffer, sizeof(buffer));
  return buffer;
}

std::string translateRect(float x, float y) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "translate(%gpx, %gpx)",
                static_cast<double>(x), static_cast<double>(y));
  return buffer;
}

std::string sizeText(float value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%gpx", static_cast<double>(value));
  return buffer;
}

void applyPreviewPart(AbcUiElement *element, PreviewPart &applied,
                      const char *color, const float rect[4]) {
  if (!element)
    return;
  const bool same = applied.Valid && applied.Color == color &&
                    applied.X == rect[0] && applied.Y == rect[1] &&
                    applied.Width == rect[2] && applied.Height == rect[3];
  if (same)
    return;
  applied.Valid = true;
  applied.Color = color;
  applied.X = rect[0];
  applied.Y = rect[1];
  applied.Width = rect[2];
  applied.Height = rect[3];
  setProperty(element, "background-color", color);
  const std::string transform = translateRect(rect[0], rect[1]);
  setProperty(element, "transform", transform.c_str());
  const std::string width = sizeText(rect[2]);
  const std::string height = sizeText(rect[3]);
  setProperty(element, "width", width.c_str());
  setProperty(element, "height", height.c_str());
}

void renderCrosshairPreview() {
  if (!Page || !Api->settings)
    return;
  AbcUiElement *box = node("crosshair-preview");
  if (!box)
    return;

  // Draft values with the shipped defaults as the fallback (a page is free to
  // omit a row; the preview then shows the documented default).
  const auto value = [](const char *cvar, float fallback) {
    AbcUiSettingRow row{};
    if (!rowFound(cvar, row) || (row.flags & ABCUI_SET_ROW_MISSING))
      return fallback;
    return row.state == ABCUI_SET_STATE_INVALID ? row.engineValue
                                                : row.draftValue;
  };
  AbcUiCrosshairLayout layout{};
  layout.thickness = value("cl_crosshairthickness", 2.0f);
  layout.length = value("cl_crosshairsize", 24.0f);
  layout.offset = value("cl_crosshairgap", 16.0f);
  layout.outlineWidth = value("cl_crosshair_outline_draw", 1.0f) > 0.0f
                            ? value("cl_crosshair_outline", 1.0f)
                            : 0.0f;
  layout.dot = value("cl_crosshairdot", 0.0f) > 0.0f ? 1 : 0;
  layout.tShape = value("cl_crosshair_t", 0.0f) > 0.0f ? 1 : 0;

  const std::string coreColor =
      rgbaText(channelValue(value("cl_crosshaircolor_r", 50.0f)),
               channelValue(value("cl_crosshaircolor_g", 250.0f)),
               channelValue(value("cl_crosshaircolor_b", 50.0f)),
               channelValue(value("cl_crosshairalpha", 200.0f)));
  const std::string outlineColor =
      rgbaText(channelValue(value("cl_crosshaircolor_outline_r", 0.0f)),
               channelValue(value("cl_crosshaircolor_outline_g", 0.0f)),
               channelValue(value("cl_crosshaircolor_outline_b", 0.0f)),
               channelValue(value("cl_crosshair_outline_alpha", 255.0f)));

  float core[5][4] = {};
  float outline[5][4] = {};
  int32_t visible[5] = {};
  Api->ui->crosshairLayout(&layout, core, outline, visible);
  for (int index = 0; index < 5; ++index) {
    const bool shown = visible[index] != 0;
    applyPreviewPart(node(kCoreIds[index]), PreviewCore[index],
                     shown ? coreColor.c_str() : kTransparentColor,
                     core[index]);
    applyPreviewPart(node(kOutlineIds[index]), PreviewOutline[index],
                     (shown && layout.outlineWidth > 0.0f)
                         ? outlineColor.c_str()
                         : kTransparentColor,
                     outline[index]);
  }
}

// ---------------------------------------------------------- model picker --

// Byte-wise tolower: the filter compares model directory names, which the
// scan takes verbatim from the filesystem (ASCII names in practice; the
// legacy picker used the same ::tolower rule).
char lowerByte(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool containsCaseInsensitive(const std::string &value,
                             const std::string &filter) {
  if (filter.empty())
    return true;
  const auto at =
      std::search(value.begin(), value.end(), filter.begin(), filter.end(),
                  [](char a, char b) { return lowerByte(a) == lowerByte(b); });
  return at != value.end();
}

// The console's ancestry walk: a press belongs to the menu when the target is
// the frame itself or one of its items.
bool insideNode(AbcUiElement *element, AbcUiElement *ancestor) {
  for (AbcUiElement *it = element; it; it = Api->ui->elementParent(it)) {
    if (it == ancestor)
      return true;
  }
  return false;
}

AbcUiElement *actionNode(AbcUiElement *element) {
  for (AbcUiElement *it = element; it; it = Api->ui->elementParent(it)) {
    if (!attributeOf(it, "data-action").empty())
      return it;
  }
  return nullptr;
}

bool favoriteOn(const char *model) {
  return Api->settings && model && *model &&
         Api->settings->favoriteContains(model) != 0;
}

void closeModelContextMenu() {
  ModelContextOpen = false;
  ModelContextModel.clear();
  setDisplay("model-picker-context-menu", false);
}

void toggleFavorite(const std::string &model) {
  if (!Api->settings || model.empty())
    return;
  if (!Api->settings->favoritesAvailable())
    return; // read-only session: the status line explains why
  Api->settings->favoriteSet(model.c_str(), favoriteOn(model.c_str()) ? 0 : 1);
}

void renderModelPicker() {
  if (!Page || !Api->settings)
    return;
  AbcUiElement *list = node("model-picker-list");
  AbcUiElement *status = node("model-picker-status");
  AbcUiElement *favOnly = node("model-picker-favonly");
  if (!list || !status || !favOnly)
    return;

  AbcUiSettingRow model{};
  const bool haveModel =
      rowFound("model", model) && !(model.flags & ABCUI_SET_ROW_MISSING);
  const std::string current = haveModel ? model.draftText : std::string();
  const int32_t totalChoices = haveModel ? model.choiceCount : 0;

  int total = 0;
  int shown = 0;
  for (int32_t index = 0; index < totalChoices; ++index) {
    char name[ABCUI_SETTINGS_TEXT_MAX];
    name[0] = '\0';
    if (!Api->settings->choiceAt("model", index, name, sizeof(name), nullptr,
                                 0))
      continue;
    ++total;
    if (ModelFavOnly && !favoriteOn(name))
      continue;
    if (!containsCaseInsensitive(name, ModelFilter))
      continue;
    ++shown;
  }

  const std::string signature =
      ModelFilter + "|" + (ModelFavOnly ? "fav" : "all") + "|" + current;
  const uint32_t generation = Api->settings->favoritesGeneration();
  const bool rebuild = signature != ModelBuiltSignature ||
                       generation != ModelBuiltFavoriteGeneration;
  if (rebuild) {
    ModelBuiltSignature = signature;
    ModelBuiltFavoriteGeneration = generation;
    std::string markup;
    for (int32_t index = 0; index < totalChoices; ++index) {
      char name[ABCUI_SETTINGS_TEXT_MAX];
      name[0] = '\0';
      if (!Api->settings->choiceAt("model", index, name, sizeof(name), nullptr,
                                   0))
        continue;
      const std::string choice = name;
      if (ModelFavOnly && !favoriteOn(name))
        continue;
      if (!containsCaseInsensitive(choice, ModelFilter))
        continue;
      const bool favorite = favoriteOn(name);
      markup += "<button class=\"model-picker-row";
      if (choice == current)
        markup += " selected";
      markup += "\" data-action=\"" + std::string(kActionModelPick) +
                "\" data-choice=\"" + escapeAttribute(choice) +
                "\"><span class=\"model-picker-name\">" + escapeText(choice) +
                "</span><span class=\"model-picker-star";
      if (favorite)
        markup += " on";
      markup += "\" data-action=\"" + std::string(kActionModelFav) +
                "\" data-choice=\"" + escapeAttribute(choice) + "\">" +
                escapeText(resolve("#Settings_ModelStar")) + "</span></button>";
    }
    if (shown == 0) {
      markup +=
          "<div class=\"model-picker-empty\">" +
          escapeText(resolve(total == 0 ? "#Settings_ModelPickerEmpty"
                                        : "#Settings_ModelPickerNoMatch")) +
          "</div>";
    }
    Api->ui->elementSetInnerRML(list, markup.c_str());
  }

  // The toggle's state and the count line are cheap writes: they follow every
  // render pass so external model/favorite changes stay honest.
  Api->ui->elementSetClass(favOnly, "on", ModelFavOnly);
  std::string statusText;
  if (!Api->settings->favoritesAvailable()) {
    statusText = resolve("#Settings_ModelFavLocked");
  } else if (total == 0) {
    statusText = resolve("#Settings_ModelPickerEmpty");
  } else {
    statusText = std::to_string(shown) + " / " + std::to_string(total) + " " +
                 resolve("#Settings_ModelPickerModels");
  }
  setInnerRml(status, escapeText(statusText));
}

bool handleModelAction(const std::string &action, AbcUiElement *entry) {
  if (action != kActionModelPick && action != kActionModelFav &&
      action != kActionModelFavOnly)
    return false;
  if (action == kActionModelFavOnly) {
    ModelFavOnly = !ModelFavOnly;
    return true;
  }
  const std::string name = attributeOf(entry, "data-choice");
  if (name.empty())
    return true; // picker intent without a payload: consumed, no-op
  if (action == kActionModelFav) {
    toggleFavorite(name);
    return true;
  }
  // The pick commits immediately: the draft feeds the shared <model> preview
  // on the next render, a single-row apply writes the cvar with the model's
  // own read-back; a refused write leaves the row dirty and the status bar
  // reports the pending change.
  if (Api->settings) {
    Api->settings->setDraftString("model", name.c_str());
    Api->settings->applyOne("model");
  }
  return true;
}

bool handleModelFilterChange(const std::string &value) {
  ModelFilter = value;
  renderModelPicker();
  return true;
}

bool handleModelDblClick(AbcUiElement *target) {
  // The double-click pair's first click already selected the row; the legacy
  // picker used the second press purely as a favorite toggle.
  AbcUiElement *entry = actionNode(target);
  if (!entry || attributeOf(entry, "data-action") != kActionModelPick)
    return false;
  const std::string name = attributeOf(entry, "data-choice");
  if (name.empty())
    return true;
  toggleFavorite(name);
  render();
  return true;
}

void openModelContextMenu(AbcUiElement *entry, float mouseX, float mouseY) {
  ModelContextModel = attributeOf(entry, "data-choice");
  if (ModelContextModel.empty())
    return;
  AbcUiElement *menu = node("model-picker-context-menu");
  if (!menu)
    return;
  // Viewport pixel coordinates (the same space as the mouse event), clamped
  // so a click near an edge still shows the whole entry. The size mirrors
  // model-picker-context-menu's fixed dp.
  float scale = 1.0f;
  if (!Api->ui->documentScale(Page, &scale) || scale <= 0.0f)
    scale = 1.0f;
  int width = 0;
  int height = 0;
  Api->ui->documentSize(Page, &width, &height);
  const float menuWidth = 104.0f * scale;
  const float menuHeight = 31.0f * scale;
  const float maxX = width > 0 ? std::max(0.0f, width - menuWidth) : 0.0f;
  const float maxY = height > 0 ? std::max(0.0f, height - menuHeight) : 0.0f;
  char left[32];
  char top[32];
  std::snprintf(left, sizeof(left), "%gpx",
                static_cast<double>(std::clamp(mouseX, 0.0f, maxX)));
  std::snprintf(top, sizeof(top), "%gpx",
                static_cast<double>(std::clamp(mouseY, 0.0f, maxY)));
  setProperty(menu, "left", left);
  setProperty(menu, "top", top);
  // The menu carries one fixed intent; its label flips with the target row's
  // current state.
  setInnerRml(node("model-picker-context-fav"),
              escapeText(resolve(favoriteOn(ModelContextModel.c_str())
                                     ? "#Settings_ModelFavRemove"
                                     : "#Settings_ModelFavAdd")));
  setDisplay(menu, true);
  ModelContextOpen = true;
}

bool handleModelPointerDown(AbcUiElement *target, int32_t button, float mouseX,
                            float mouseY) {
  AbcUiElement *menu = node("model-picker-context-menu");
  if (button == 1) {
    // Right press over a picker row opens the favorite menu at the pointer;
    // anywhere else an open menu just closes.
    AbcUiElement *entry = actionNode(target);
    if (entry && attributeOf(entry, "data-action") == kActionModelPick) {
      openModelContextMenu(entry, mouseX, mouseY);
      return false; // the press itself selects nothing
    }
    if (ModelContextOpen)
      closeModelContextMenu();
    return false;
  }
  // Any left press outside the menu dismisses it (the menu item's own click
  // path closes it after acting).
  if (ModelContextOpen && menu && !insideNode(target, menu))
    closeModelContextMenu();
  return false;
}

bool handleModelContextAction(const std::string &action) {
  if (action != kActionModelContextFav)
    return false;
  if (!ModelContextModel.empty())
    toggleFavorite(ModelContextModel);
  closeModelContextMenu();
  return true;
}

// ----------------------------------------------------------- spray picker --

std::string sprayStatusText() {
  char buffer[ABCUI_SETTINGS_TEXT_MAX];
  buffer[0] = '\0';
  if (!Api->settings || !Api->settings->sprayStatus(buffer, sizeof(buffer)))
    return {};
  return buffer;
}

std::string sprayPreviewText() {
  char buffer[512];
  buffer[0] = '\0';
  if (!Api->settings || !Api->settings->sprayPreviewUri(buffer, sizeof(buffer)))
    return {};
  return buffer;
}

int32_t attributeIndex(AbcUiElement *element, const char *name) {
  const std::string text = attributeOf(element, name);
  return text.empty() ? -1 : std::atoi(text.c_str());
}

void renderSprayBrowser() {
  if (!Page)
    return;
  if (AbcUiElement *path = node("spray-file-path"))
    setInnerRml(path, escapeText(Browser.directory()));
  if (AbcUiElement *roots = node("spray-file-roots")) {
    std::string markup;
    int index = 0;
    for (const auto &root : Browser.roots()) {
      markup += "<button class=\"spray-file-root\" "
                "data-action=\"" +
                std::string(kActionSprayRoot) + "\" data-root=\"" +
                std::to_string(index++) + "\">" + escapeText(root.Label) +
                "</button>";
    }
    Api->ui->elementSetInnerRML(roots, markup.c_str());
  }
  if (AbcUiElement *list = node("spray-file-list")) {
    std::string markup;
    int index = 0;
    for (const auto &item : Browser.entries()) {
      markup += "<div class=\"spray-file-row";
      if (!item.Directory && item.Name == SpraySelected)
        markup += " selected";
      markup +=
          "\" data-action=\"" + std::string(kActionSprayOpen) +
          "\" data-entry=\"" + std::to_string(index++) + "\">" +
          "<span class=\"spray-file-name\">" + escapeText(item.Name) +
          "</span><span class=\"spray-file-tag\">" +
          escapeText(resolve(item.Directory ? "#Settings_SprayMakerFolder"
                                            : "#Settings_SprayMakerImage")) +
          "</span></div>";
    }
    if (markup.empty())
      markup = "<div class=\"spray-file-empty\">" +
               escapeText(resolve("#Settings_SprayMakerEmpty")) + "</div>";
    Api->ui->elementSetInnerRML(list, markup.c_str());
  }
  if (AbcUiElement *status = node("spray-file-status")) {
    setInnerRml(status, escapeText(Browser.error().empty()
                                       ? resolve("#Settings_SprayMakerHint")
                                       : Browser.error()));
  }
}

void openSprayBrowser() {
  if (!Page)
    return;
  hideLayers();
  SprayOpen = true;
  SpraySelected.clear();
  Browser.setRoots(files::FileBrowser::defaultRoots());
  // Start at the game folder (the process working directory) - the first
  // root - and fall back to the first readable one.
  for (const auto &root : Browser.roots()) {
    if (Browser.open(root.Path))
      break;
  }
  renderSprayBrowser();
  setDisplay(kLayerSprayFile, true);
  ActiveLayer = kLayerSprayFile;
  if (AbcUiElement *focus = node("spray-file-cancel"))
    Api->ui->elementFocus(focus);
}

void closeSprayBrowser() {
  hideLayers();
}

void handleSprayAction(const std::string &action, AbcUiElement *entry) {
  if (action == kActionSprayOpen) {
    const int32_t index = attributeIndex(entry, "data-entry");
    const auto &entries = Browser.entries();
    if (index < 0 || static_cast<std::size_t>(index) >= entries.size())
      return;
    const auto &picked = entries[static_cast<std::size_t>(index)];
    if (picked.Directory) {
      Browser.enter(picked.Name);
      SpraySelected.clear();
    } else {
      // A file click only selects; the confirm button starts the maker.
      SpraySelected = picked.Name;
    }
  } else if (action == kActionSprayUp) {
    Browser.openParent();
    SpraySelected.clear();
  } else if (action == kActionSprayRoot) {
    const int32_t index = attributeIndex(entry, "data-root");
    const auto &roots = Browser.roots();
    if (index >= 0 && static_cast<std::size_t>(index) < roots.size()) {
      Browser.open(roots[static_cast<std::size_t>(index)].Path);
      SpraySelected.clear();
    }
  } else if (action == kActionSprayConfirm) {
    if (SpraySelected.empty() || !Api->settings)
      return;
    std::string path = Browser.directory();
    if (!path.empty() && path.back() != '\\' && path.back() != '/')
      path += '\\';
    path += SpraySelected;
    // The maker runs framework-side (decode/encode/write + the preview
    // upload) and stores its answer; the page reads it back on the next
    // render. A synchronous request means the layer can close right here.
    Api->settings->sprayMake(path.c_str());
    hideLayers();
    render();
    refocusDefault();
    return;
  }
  renderSprayBrowser();
}

void renderSprayMaker() {
  if (!Page)
    return;
  const std::string status = sprayStatusText();
  setInnerRml(node("spray-maker-status"), escapeText(status));
  AbcUiElement *image = node("spray-made-preview");
  if (!image)
    return;
  const std::string uri = sprayPreviewText();
  if (uri.empty()) {
    setDisplay(image, false);
    return;
  }
  if (attributeOf(image, "src") != uri)
    Api->ui->elementSetAttribute(image, "src", uri.c_str());
  setDisplay(image, true);
}

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
    text = resolve("#Settings_StatusClean");
  } else if (failed == 0) {
    text = resolve("#Settings_Applied");
    if (queued > 0)
      text += " | " + std::to_string(queued) + " " +
              resolve("#Settings_QueuedPlural");
  } else {
    text = std::to_string(applied) + "/" + std::to_string(applied + failed) +
           " " + resolve("#Settings_ApplyPartial");
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
      renderRow(row, skipRow && std::strcmp(row.cvar, skipRow) == 0);
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

// ----------------------------------------------------- presentation ------

void onConfirmClick(AbcUiElement *target);
void onClick(AbcUiElement *target);
void onChange(AbcUiElement *target, const std::string &value);
void onDocumentReady(void *);
void onDocumentHide(void *);
void onDocumentClose(void *);
void onConfirmReady(void *);
void onConfirmClose(void *);

void present(bool focusDefault) {
  if (!Page || !Api->settings)
    return;
  Api->documents->show(Page);
  ensureSkeleton();
  if (!Presented) {
    // Fresh opening: the four models re-read their engine truth once, the
    // page then projects the settled state (the rest of the page's beats are
    // change-gated).
    Api->settings->snapshot();
    Presented = true;
    render();
  }
  if (focusDefault)
    refocusDefault();
}

void dismiss() {
  if (Page && Api->documents->visible(Page))
    Api->documents->hide(Page);
  Presented = false;
}

void renderConfirm(bool focusDefault) {
  if (!Confirm)
    return;
  AbcUiVideoFacts facts{};
  if (!videoFacts(facts))
    return;
  // The headline states the mode the engine ACTUALLY runs, plus what the
  // engine was asked to switch to (the request alone proves nothing).
  const std::string actualText =
      facts.actualKnown ? describeMode(facts.actualWidth, facts.actualHeight,
                                       facts.actualWindowed)
                        : resolve("#VideoConfirm_Unknown");
  setInnerRml(confirmNode("videoconfirm-actual"), escapeText(actualText));
  setInnerRml(
      confirmNode("videoconfirm-requested"),
      escapeText(describeMode(facts.requestedWidth, facts.requestedHeight,
                              facts.requestedWindowed)));
  const char *lead = "#VideoConfirm_Lead";
  switch (facts.confirmKind) {
  case ABCUI_VIDEO_CONFIRM_APPLIED:
    lead = "#VideoConfirm_Lead";
    break;
  case ABCUI_VIDEO_CONFIRM_NOT_APPLIED:
    lead = "#VideoConfirm_NotApplied";
    break;
  default:
    lead = "#VideoConfirm_Unknown";
    break;
  }
  setInnerRml(confirmNode("videoconfirm-lead"), escapeText(resolve(lead)));
  std::string timeout = resolve("#VideoConfirm_TimeoutTime");
  const std::string marker = "{0}";
  const std::string seconds = std::to_string(facts.remainingSeconds);
  const std::size_t at = timeout.find(marker);
  if (at != std::string::npos)
    timeout.replace(at, marker.size(), seconds);
  else
    timeout += " " + seconds;
  setInnerRml(confirmNode("videoconfirm-timeout"), escapeText(timeout));

  // The Keep entry is only offered when the switch actually took effect; the
  // model refuses anything else anyway (fail-closed projection).
  AbcUiElement *keep = confirmNode("videoconfirm-keep");
  const bool canKeep = facts.confirmKind == ABCUI_VIDEO_CONFIRM_APPLIED;
  Api->ui->elementSetClass(keep, "video-disabled", !canKeep);
  Api->ui->elementSetDisabled(keep, !canKeep);
  if (focusDefault) {
    if (AbcUiElement *focus = confirmNode("videoconfirm-revert"))
      Api->ui->elementFocus(focus);
  }
}

bool projectVideoConfirm() {
  if (!Confirm)
    return false;
  AbcUiVideoFacts facts{};
  const bool awaiting = videoFacts(facts) && facts.awaitingConfirm;
  const bool visible = Api->documents->visible(Confirm) != 0;
  if (awaiting) {
    // The projection is change-gated on the page generation: the facts
    // re-query the engine's actual mode, so a per-frame rewrite would be
    // wasted work while the countdown ticks.
    const bool fresh = !visible;
    if (fresh)
      Api->documents->show(Confirm);
    const uint32_t revision = Api->settings ? Api->settings->revision() : 0;
    if (fresh || revision != ConfirmRenderedRevision) {
      ConfirmRenderedRevision = revision;
      renderConfirm(fresh);
    }
  } else if (visible) {
    Api->documents->hide(Confirm);
    ConfirmRenderedRevision = ~0u;
  }
  return awaiting;
}

void onMenuState(void *, const void *payload) {
  const auto *state = static_cast<const AbcUiMenuState *>(payload);
  const bool wanted =
      state && state->up && state->page == ABCUI_MENU_PAGE_SETTINGS;
  if (!wanted) {
    dismiss();
    return;
  }
  AbcUiVideoFacts facts{};
  // The display confirmation outranks every other surface: the page stays
  // hidden until it resolves.
  if (videoFacts(facts) && facts.awaitingConfirm)
    return;
  present(state->overlays == 0);
}

void onMenuPaint(void *, const void *payload) {
  const auto *state = static_cast<const AbcUiMenuState *>(payload);
  const bool awaitingConfirm = projectVideoConfirm();
  const bool wanted =
      state && state->up && state->page == ABCUI_MENU_PAGE_SETTINGS;
  if (!wanted) {
    dismiss();
    return;
  }
  if (awaitingConfirm) {
    dismiss();
    return;
  }
  if (!Page)
    return;
  if (!Api->documents->visible(Page)) {
    present(state->overlays == 0);
    return;
  }
  if (!Api->settings)
    return;
  ensureSkeleton();
  // Model beats that need a per-frame check (cheap while nothing is
  // pending): the bind apply's queued rows and the command-channel settle.
  if (Api->settings->bindHasQueued())
    Api->settings->bindVerify();
  if (Api->settings->hasQueued())
    Api->settings->snapshotRows();
  AbcUiVoiceFacts voice{};
  if (voiceFacts(voice) && voice.testing)
    renderVoiceSectionImpl();
  if (Api->settings->revision() != RenderedRevision)
    render();
}

int32_t onEscape(void *, const void *) {
  if (!Page || !Api->documents->visible(Page))
    return 0;
  return requestLeave() ? 1 : 0;
}

// -------------------------------------------------------------- events ----

bool rowEditValue(AbcUiElement *target, std::string &value) {
  char buffer[ABCUI_SETTINGS_TEXT_MAX * 4];
  buffer[0] = '\0';
  if (!target || Api->ui->elementValue(target, buffer, sizeof(buffer)) <= 0)
    return false;
  value = buffer;
  return true;
}

void onChange(AbcUiElement *target) {
  // Re-entrancy guard: the selects dispatch change while render() builds
  // their options - programmatic churn from the projection itself is NOT user
  // input and commits nothing.
  if (InRender || !target || !Api->settings)
    return;
  const std::string id = Api->ui->elementId(target);
  // RGBA colour pickers: one change carries four channel values, so the
  // row-keyed commit below cannot express it. The picker is not a form
  // control: its canonical value rides the element's own value attribute
  // (written before the change is dispatched).
  if (bindingFor(id.c_str())) {
    handleColorChange(id.c_str(), attributeOf(target, "value").c_str());
    render();
    return;
  }
  std::string value;
  if (!rowEditValue(target, value))
    return;
  // T12 resolution select: the value is "WxH" from the enumerated option set;
  // the model validates it again before staging.
  if (id == "video-resolution") {
    AbcUiVideoFacts facts{};
    if (videoFacts(facts) && facts.available && !facts.awaitingConfirm) {
      const std::size_t separator = value.find('x');
      if (separator != std::string::npos) {
        const int width = std::atoi(value.substr(0, separator).c_str());
        const int height = std::atoi(value.substr(separator + 1).c_str());
        // Re-entrancy guard: a programmatic value write can re-enter here
        // with the value already staged.
        const int currentWidth =
            facts.hasStaged ? facts.stagedWidth : facts.actualWidth;
        const int currentHeight =
            facts.hasStaged ? facts.stagedHeight : facts.actualHeight;
        if (currentWidth != width || currentHeight != height)
          Api->settings->videoStageResolution(width, height);
      }
    }
    render();
    return;
  }
  if (id == "model-picker-filter") {
    // The filter commits on the control's change (Enter/focus loss, like
    // every text row); the picker re-projects its own list only.
    handleModelFilterChange(value);
    return;
  }
  if (id == "voice-mic-volume") {
    // Mic volume commits through the display scale (0..100 -> 0..1); the
    // model clamps and marks the draft dirty, the device write waits for
    // Apply like every other row.
    AbcUiVoiceFacts facts{};
    float parsed = 0.0f;
    if (voiceFacts(facts) && facts.available &&
        parseNumber(value.c_str(), parsed))
      Api->settings->voiceSetVolumeDraft(parsed / 100.0f);
    VoiceVolumeSkip = target;
    if (!InRender)
      renderVoiceSection();
    VoiceVolumeSkip = nullptr;
    return;
  }
  // Row commit: the data-row attribute lives on the control or an ancestor.
  AbcUiElement *entry = target;
  while (entry && attributeOf(entry, "data-row").empty())
    entry = Api->ui->elementParent(entry);
  if (!entry)
    return;
  const std::string rowKey = attributeOf(entry, "data-row");
  if (rowKey.empty())
    return;
  AbcUiSettingRow row{};
  if (!rowFound(rowKey.c_str(), row))
    return;
  // Select rows (kEnum/kStringEnum) commit through their value string and
  // text rows (kText) through the validated string door; the model
  // re-validates against the option set / charset either way.
  if (row.kind == ABCUI_SET_KIND_STRING_ENUM ||
      row.kind == ABCUI_SET_KIND_TEXT) {
    Api->settings->setDraftString(rowKey.c_str(), value.c_str());
  } else if (row.kind == ABCUI_SET_KIND_ENUM) {
    float parsed = 0.0f;
    if (parseNumber(value.c_str(), parsed))
      Api->settings->setDraftEnum(rowKey.c_str(), parsed);
  } else {
    Api->settings->setDraftText(rowKey.c_str(), value.c_str());
  }
  if (!InRender)
    render(rowKey.c_str());
}

void onClick(AbcUiElement *target) {
  if (!target || !Api->settings)
    return;
  AbcUiElement *entry = actionNode(target);
  if (!entry)
    return;
  const std::string action = attributeOf(entry, "data-action");
  const std::string rowKey = attributeOf(entry, "data-row");
  if (action.empty())
    return;

  // The model picker's intents come first - the list rows are generic
  // data-action nodes the chain below would drop. The context menu item acts
  // and closes the menu itself.
  if (handleModelContextAction(action)) {
    if (!InRender)
      render();
    return;
  }
  if (handleModelAction(action, entry)) {
    if (!InRender)
      render();
    return;
  }
  if (action == "settings-page") {
    selectPage(attributeOf(entry, "data-page"));
    return;
  }
  // Spray maker: the picker layer. Its intents are handled by the picker
  // state; the page keeps the layer frame.
  if (action == kActionSprayChoose) {
    openSprayBrowser();
    return;
  }
  if (action == kActionSprayOpen || action == kActionSprayUp ||
      action == kActionSprayRoot || action == kActionSprayConfirm) {
    handleSprayAction(action, entry);
    return;
  }
  if (action == kActionSprayCancel) {
    closeSprayBrowser();
    refocusDefault();
    return;
  }
  if (action == kActionApply) {
    applyAndReport();
  } else if (action == kActionReset) {
    showLayer(kLayerReset); // restore-defaults always confirms first
  } else if (action == kActionResetYes) {
    resetCurrentPage();
    hideLayers();
    render();
    refocusDefault();
  } else if (action == kActionResetNo) {
    hideLayers();
    refocusDefault();
  } else if (action == kActionLeaveDiscard) {
    discardAndReturn();
  } else if (action == kActionLeaveStay) {
    hideLayers();
    refocusDefault();
  } else if (action == kActionToggle && !rowKey.empty()) {
    AbcUiSettingRow row{};
    if (rowFound(rowKey.c_str(), row) && !(row.flags & ABCUI_SET_ROW_MISSING)) {
      // Toggle kinds: kBool threshold, kNegate sign view, kCommandToggle
      // target state - all through setDraftBool.
      const bool current =
          row.state == ABCUI_SET_STATE_INVALID
              ? (row.kind == ABCUI_SET_KIND_NEGATE ? row.engineValue < 0.0f
                                                   : isOn(row.engineValue))
              : toggleDraftOn(row);
      Api->settings->setDraftBool(rowKey.c_str(), current ? 0 : 1);
    }
    render();
  } else if (action == kActionVoiceTest) {
    // Start/stop the microphone test. The engine refusing the start (no
    // device) leaves the state untouched - the status keeps the idle text.
    AbcUiVoiceFacts facts{};
    if (voiceFacts(facts) && facts.available) {
      if (facts.testing)
        Api->settings->voiceStopTest();
      else
        Api->settings->voiceStartTest();
    }
    renderVoiceSection();
  } else if (action == kActionVoiceBoost) {
    AbcUiVoiceFacts facts{};
    if (voiceFacts(facts) && facts.available) {
      const bool on =
          facts.micBoostDirty ? facts.micBoostDraft != 0 : facts.micBoost != 0;
      Api->settings->voiceSetBoostDraft(on ? 0 : 1);
    }
    renderVoiceSection();
  } else if (action == kActionAdopt && !rowKey.empty()) {
    Api->settings->adopt(rowKey.c_str());
    render();
  } else if (action == kActionBindCapture && !rowKey.empty()) {
    // Start a capture session: the model takes the row, the framework takes
    // the router token (kCapture tier) and the input clamp. A click on an
    // already-capturing row is a no-op (the model refuses a second session).
    Api->settings->bindBeginCapture(std::atoi(rowKey.c_str()));
    render();
  } else if (action == kActionBindClear && !rowKey.empty()) {
    Api->settings->bindClear(std::atoi(rowKey.c_str()));
    render();
  } else if (action == kActionBindRemove && !rowKey.empty()) {
    // data-row carries "<action>:<keyname>" for the single-key remove.
    const std::size_t separator = rowKey.find(':');
    if (separator != std::string::npos) {
      Api->settings->bindRemove(std::atoi(rowKey.substr(0, separator).c_str()),
                                rowKey.substr(separator + 1).c_str());
    }
    render();
  } else if (action == kActionBindReset) {
    // Bind defaults always confirm first; without a bind model the whole
    // section is absent, so the intent is a no-op (fail-safe).
    if (Api->settings->bindReadsAvailable())
      showLayer(kLayerBindReset);
  } else if (action == kActionBindResetYes) {
    Api->settings->bindResetDefaults();
    hideLayers();
    render();
    refocusDefault();
  } else if (action == kActionBindResetNo) {
    hideLayers();
    refocusDefault();
  } else if (action == kActionBindConflictYes) {
    Api->settings->bindResolveConflict(1);
    render(); // the layer hides itself once the conflict is cleared
    refocusDefault();
  } else if (action == kActionBindConflictNo) {
    Api->settings->bindResolveConflict(0);
    render();
    refocusDefault();
  } else if (action == kActionVideoApply) {
    // The restart-driving apply belongs to the framework transaction, which
    // also presents the confirmation afterwards.
    Api->settings->videoApply();
    render();
  } else if (action == kActionVideoWindowed) {
    AbcUiVideoFacts facts{};
    if (videoFacts(facts) && facts.available && !facts.awaitingConfirm) {
      const bool windowed = facts.hasStaged ? facts.stagedWindowed != 0
                                            : facts.actualWindowed != 0;
      Api->settings->videoStageWindowed(windowed ? 0 : 1);
    }
    render();
  } else if (action == kActionReturn) {
    if (!requestLeave())
      Api->menu->back(); // clean: the menu owns the navigation
  }
  // Unknown actions are dropped: the page only binds its own names.
}

int32_t onRootClick(void *, AbcUiElement *target,
                    const AbcUiElementEvent *event) {
  if (!event || !target || std::strcmp(event->type, "click") != 0)
    return 0;
  onClick(target);
  return 0;
}

int32_t onRootChange(void *, AbcUiElement *target,
                     const AbcUiElementEvent *event) {
  if (!event || !target || std::strcmp(event->type, "change") != 0)
    return 0;
  onChange(target);
  return 0;
}

int32_t onRootDblClick(void *, AbcUiElement *target,
                       const AbcUiElementEvent *event) {
  if (!event || !target || std::strcmp(event->type, "dblclick") != 0)
    return 0;
  handleModelDblClick(target);
  return 0;
}

int32_t onRootMouseDown(void *, AbcUiElement *target,
                        const AbcUiElementEvent *event) {
  if (!event || !target || std::strcmp(event->type, "mousedown") != 0)
    return 0;
  if (event->button == 1)
    handleModelPointerDown(target, 1, event->mouseX, event->mouseY);
  else if (event->button == 0)
    handleModelPointerDown(target, 0, event->mouseX, event->mouseY);
  return 0;
}

void onConfirmClick(AbcUiElement *target) {
  if (!target || !Api->settings)
    return;
  AbcUiElement *entry = actionNode(target);
  if (!entry)
    return;
  const std::string action = attributeOf(entry, "data-action");
  if (action == "video-keep") {
    Api->settings->videoKeep();
  } else if (action == "video-revert") {
    Api->settings->videoRevert();
  }
}

int32_t onConfirmClickEvent(void *, AbcUiElement *target,
                            const AbcUiElementEvent *event) {
  if (!event || !target || std::strcmp(event->type, "click") != 0)
    return 0;
  onConfirmClick(target);
  return 0;
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

void onConfirmReady(void *) {
  if (!Confirm)
    return;
  ConfirmRoot = Api->documents->root(Confirm);
  if (ConfirmRoot)
    ConfirmClickBinding = Api->ui->elementAddListener(
        ConfirmRoot, "click", &onConfirmClickEvent, nullptr);
}

void onConfirmClose(void *) {
  if (ConfirmRoot && ConfirmClickBinding)
    Api->ui->elementRemoveListener(ConfirmRoot, "click", ConfirmClickBinding);
  ConfirmClickBinding = 0;
  ConfirmRoot = nullptr;
}

} // namespace

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
