#pragma once

// Page-internal contract of the abcui.gameui settings surface. Each settings
// translation unit owns one responsibility (page shell, rows, bindings, video,
// voice, colours, previews, model picker, spray picker, events, presentation,
// confirmation dialog) and this header is the seam between them: the page
// state they all project, the shared DOM/model helpers and the section entry
// points. Nothing here is module ABI - the module's only exported entry stays
// AbcUiModule_Main in GameUiModule.cpp.

#include "../shared/PageShared.h"
#include "SettingsFileBrowser.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace settingspage {

// ------------------------------------------------------------- constants --

constexpr const char *kDocumentName = "menusettings";
constexpr const char *kDocumentPath =
    "mod://abcui.gameui/gameui/rml/settings.rml";
constexpr const char *kConfirmName = "videoconfirm";
constexpr const char *kConfirmPath =
    "mod://abcui.gameui/gameui/rml/videoconfirm.rml";

// data-action values (the delegated click/change dispatch keys on them).
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

// Page-internal layer DOM ids.
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

constexpr const char *kTransparentColor = "rgba(0, 0, 0, 0)";

// ---------------------------------------------------------------- state ----

extern AbcUiDocument *Page;
extern AbcUiDocument *Confirm;
extern AbcUiElement *Root;
extern AbcUiElement *ConfirmRoot;
extern uint64_t ClickBinding;
extern uint64_t ChangeBinding;
extern uint64_t DblClickBinding;
extern uint64_t MouseDownBinding;
extern uint64_t ConfirmClickBinding;
extern AbcUiSubscription StateSubscription;
extern AbcUiSubscription PaintSubscription;
extern AbcUiSubscription EscapeSubscription;

// Page structure: the navigation and the row skeletons are built once per
// document (a rebuild drops hover/focus), then only re-projected.
extern bool Built;
extern bool Presented;
extern std::string ActivePage;
// "" when no layer is up, else the layer's DOM id.
extern std::string ActiveLayer;
// Re-entrancy guard: a projection that rewrites a select's options dispatches
// change while it runs; that programmatic churn is not user input.
extern bool InRender;
extern uint32_t RenderedRevision;
// Counts the skeleton DOM was last built from. The document can become ready
// before the framework models are loaded (the page is created at context
// ready, while the models load from onInit), so the first build may see an
// empty bind list; render() rebuilds when a count moves.
extern int BuiltRowCount;
extern int BuiltBindCount;
extern int BuiltPageCount;
// The generation last projected into the confirmation dialog (its facts
// re-query the engine, so the dialog re-renders on change only).
extern uint32_t ConfirmRenderedRevision;

// Option sets last projected into the select rows (cvar -> values): the
// widget is rebuilt only when the set actually changed.
extern std::vector<std::pair<std::string, std::vector<std::string>>>
    SelectShown;
// The resolution options last projected into the display select.
extern std::vector<std::pair<int, int>> VideoOptionsShown;
// The hue last painted on one row's drag thumb (cvar -> "rgb(...)").
extern std::vector<std::pair<std::string, std::string>> HueShown;
// The element of the voice-volume change commit in flight (its caret/IME
// state must survive the status refresh).
extern AbcUiElement *VoiceVolumeSkip;

// Model picker page state (the filter and the favorites-only toggle live as
// long as the document).
extern std::string ModelFilter;
extern bool ModelFavOnly;
extern std::string ModelBuiltSignature;
extern uint32_t ModelBuiltFavoriteGeneration;
extern bool ModelContextOpen;
extern std::string ModelContextModel;

// Spray picker page state (the directory model; the maker runs framework-side).
extern files::FileBrowser Browser;
extern bool SprayOpen;
extern std::string SpraySelected;

extern PreviewPart PreviewCore[5];
extern PreviewPart PreviewOutline[5];

// ------------------------------------------------------- shared helpers ----

AbcUiElement *node(const char *id);
AbcUiElement *node(const std::string &id);
AbcUiElement *confirmNode(const char *id);
void setInnerRml(AbcUiElement *element, const std::string &rml);
void setInnerRml(const char *id, const std::string &rml);
void setText(const char *id, const std::string &text);
void setDisplay(AbcUiElement *element, bool visible);
void setDisplay(const char *id, bool visible);
void setProperty(AbcUiElement *element, const char *name, const char *value);
std::string numberText(float value);
bool isOn(float value);
bool parseNumber(const char *text, float &out);
bool isToggleKind(int32_t kind);
bool isSelectKind(int32_t kind);
bool toggleDraftOn(const AbcUiSettingRow &row);
std::string logoColorName(const std::string &value);
std::string fileStem(const std::string &value);
const char *safeFocusForLayer(const char *id);
bool layerVisible();
bool rowFound(const char *cvar, AbcUiSettingRow &out);
bool rowById(int32_t index, AbcUiSettingRow &out);
bool videoFacts(AbcUiVideoFacts &out);
bool voiceFacts(AbcUiVoiceFacts &out);
bool currentVideoStaged();
bool currentVoiceDirty();
bool isDirty();

// ----------------------------------------------------------------- page ----

void hideLayers();
void showLayer(const char *id);
void refocusDefault();
int pageCount();
bool pageAt(int32_t index, AbcUiSettingsPage &out);
bool sectionAt(int32_t page, int32_t index, AbcUiSettingsSection &out);
std::string sectionComponent(int32_t page, int32_t section, int32_t index);
std::string pageCvar(int32_t page, int32_t index);
int pageIndexOf(const std::string &id);
bool pageHasComponent(int32_t page, const char *name);
bool hasComponent(const char *name);
bool sectionHasComponent(int32_t page, int32_t section, const char *name);
bool ownsColorCvar(const std::string &cvar);
void projectPage();
void buildNavigation();
bool selectPage(const std::string &id);
void ensureSkeleton();
void render(const char *skipRow = nullptr);
void applyAndReport();
void resetCurrentPage();
bool requestLeave();
void discardAndReturn();

// ----------------------------------------------------------------- rows ----

void AppendRowRow(const AbcUiSettingRow &row, std::string &markup);
void projectWidgets();
void OpenCard(std::string &markup, const std::string &cardId,
              const std::string &titleToken, bool hasPreview, bool hasBlock,
              bool stacked, bool twoUp);
void CloseCard(std::string &markup, const std::string &cardId, bool hasPreview);
void buildRows();
void renderRow(AbcUiSettingRow &row, bool skipInput);
void setStatusSummary();

// --------------------------------------------------------------- colours --

bool parseHex(const char *text, unsigned char out[4]);
std::string formatHex(const unsigned char color[4]);
int channelValue(float value);
const ColorBinding *bindingFor(const char *elementId);
void renderColorRows();
void handleColorChange(const char *elementId, const char *value);

// -------------------------------------------------------------- bindings --

bool bindHeaderIsRule(const AbcUiBindAction &row);
std::string bindLabelText(const std::string &label);
std::string bindActionLabel(const AbcUiBindAction &row);
void buildBindRows();
void renderBindRow(const AbcUiBindAction &row);
void renderBindConflict();
void setBindStatusSummary();

// ----------------------------------------------------------------- video --

std::string describeMode(int32_t width, int32_t height, int32_t windowed);
void renderVideoSection();
void renderConfirm(bool focusDefault);
bool projectVideoConfirm();
void onConfirmClick(AbcUiElement *target);

// ----------------------------------------------------------------- voice --

void renderVoiceSectionImpl();
void renderVoiceSection();

// -------------------------------------------------------------- previews --

void setPreviewSource(const char *id, const std::string &uri);
void renderPreviews();
std::string rgbaText(int red, int green, int blue, int alpha);
std::string translateRect(float x, float y);
std::string sizeText(float value);
void applyPreviewPart(AbcUiElement *element, PreviewPart &applied,
                      const char *color, const float rect[4]);
void renderCrosshairPreview();

// ---------------------------------------------------------- model picker --

char lowerByte(char c);
bool containsCaseInsensitive(const std::string &value,
                             const std::string &filter);
bool insideNode(AbcUiElement *element, AbcUiElement *ancestor);
AbcUiElement *actionNode(AbcUiElement *element);
bool favoriteOn(const char *model);
void closeModelContextMenu();
void toggleFavorite(const std::string &model);
void renderModelPicker();
bool handleModelAction(const std::string &action, AbcUiElement *entry);
bool handleModelFilterChange(const std::string &value);
bool handleModelDblClick(AbcUiElement *target);
void openModelContextMenu(AbcUiElement *entry, float mouseX, float mouseY);
bool handleModelPointerDown(AbcUiElement *target, int32_t button, float mouseX,
                            float mouseY);
bool handleModelContextAction(const std::string &action);

// ----------------------------------------------------------- spray picker --

std::string sprayStatusText();
std::string sprayPreviewText();
int32_t attributeIndex(AbcUiElement *element, const char *name);
void renderSprayBrowser();
void openSprayBrowser();
void closeSprayBrowser();
void handleSprayAction(const std::string &action, AbcUiElement *entry);
void renderSprayMaker();

// ---------------------------------------------------------- presentation --

void present(bool focusDefault);
void dismiss();
void onMenuState(void *, const void *payload);
void onMenuPaint(void *, const void *payload);
int32_t onEscape(void *, const void *);

// ---------------------------------------------------------------- events --

bool rowEditValue(AbcUiElement *target, std::string &value);
void onChange(AbcUiElement *target);
void onClick(AbcUiElement *target);
int32_t onRootClick(void *, AbcUiElement *target,
                    const AbcUiElementEvent *event);
int32_t onRootChange(void *, AbcUiElement *target,
                     const AbcUiElementEvent *event);
int32_t onRootDblClick(void *, AbcUiElement *target,
                       const AbcUiElementEvent *event);
int32_t onRootMouseDown(void *, AbcUiElement *target,
                        const AbcUiElementEvent *event);
int32_t onConfirmClickEvent(void *, AbcUiElement *target,
                            const AbcUiElementEvent *event);

// ------------------------------------------------------------- lifecycle --

void onDocumentReady(void *);
void onDocumentHide(void *);
void onDocumentClose(void *);
void onConfirmReady(void *);
void onConfirmClose(void *);

} // namespace settingspage
