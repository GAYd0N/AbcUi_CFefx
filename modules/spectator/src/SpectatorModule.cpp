// abcui.spectator - the observer bottom bar rebuilt in RmlUi.
//
// The native SpectatorPanel (TeamFortressViewport child, not a gHUD element)
// is enabled from the local player's entity_state iuser1/iuser2: the engine
// copies them into g_iUser1/g_iUser2 in HUD_ProcessPlayerState, the viewport's
// paintBackground copies them into the viewport and calls
// SpectatorPanel::Update, which shows the bar whenever the mode is non-zero.
// The framework exposes the same pair through AbcUiHudState.spectateMode /
// spectateTarget; the module claims ABCUI_NATIVE_HUD_SPECTATOR so the VGUI
// paint gate skips the native subtree while this document is on screen.
#include "../../common/UiText.h"
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <cmath>
#include <string>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};
bool DocumentReady = false;
std::string LastKey;

constexpr const char *kPanelName = "spectator";
constexpr const char *kDocumentPath =
    "mod://abcui.spectator/hud/rml/spectator.rml";
constexpr const char *kSpectatorCvar = "abc_ui_spectator";

AbcUiCvarHandle SpectatorEnabled = nullptr;

AbcUiDocument *panelDocument() {
  return Api ? Api->ui->panelDocument(kPanelName) : nullptr;
}

bool enabled() {
  return !SpectatorEnabled ||
         Api->console->cvarValue(SpectatorEnabled, 1.0f) > 0.0f;
}

void setVisible(AbcUiDocument *doc, bool visible) {
  if (!doc)
    return;
  const bool isVisible = Api->ui->documentVisible(doc) != 0;
  if (visible && !isVisible)
    Api->ui->documentShow(doc);
  else if (!visible && isVisible)
    Api->ui->documentHide(doc);
}

void setDisplay(const char *id, const char *value) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (AbcUiElement *element = Api->ui->querySelector(doc, id))
    Api->ui->elementSetProperty(element, "display", value);
}

void setInner(const char *id, const std::string &rml) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (AbcUiElement *element = Api->ui->querySelector(doc, id))
    Api->ui->elementSetInnerRML(element, rml.c_str());
}

std::string escape(const std::string &text) {
  return moduleui::escapeText(Api, text);
}

std::string translateOr(const char *token, const char *fallback) {
  char buffer[256] = {};
  if (Api->ui->translate(token, buffer, sizeof(buffer)))
    return buffer;
  return fallback;
}

// Native SpectatorPanel::Update mapping (the mode enum is not the titles'
// numbering): 1 -> Spec_Mode2, 2 -> Spec_Mode3, 3 -> Spec_Mode1, 4 -> Mode4.
const char *modeToken(int mode) {
  switch (mode) {
  case 1:
    return "Spec_Mode2";
  case 2:
    return "Spec_Mode3";
  case 3:
    return "Spec_Mode1";
  case 4:
    return "Spec_Mode4";
  default:
    return nullptr;
  }
}

void updatePanel() {
  if (!Api || !DocumentReady)
    return;
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;

  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);

  if (!enabled() || state.spectateMode <= 0) {
    setVisible(doc, false);
    LastKey.clear();
    return;
  }

  const int mode = state.spectateMode;
  const int targetIndex = state.spectateTarget;
  AbcUiPlayerInfo target{};
  target.structSize = sizeof(target);
  const bool targetInRange =
      targetIndex >= 1 && targetIndex <= ABCUI_MAX_PLAYERS;
  const bool targetKnown =
      targetInRange && Api->players->get(targetIndex, &target) != 0;
  const bool dead = targetKnown && target.health <= 0.0f;
  const int health =
      targetKnown ? static_cast<int>(std::lround(target.health)) : 0;
  const int armor =
      targetKnown ? static_cast<int>(std::lround(target.armor)) : 0;
  const std::string name =
      targetKnown ? std::string(target.name) : std::string();

  const std::string key = std::to_string(mode) + "|" +
                          std::to_string(targetIndex) + "|" +
                          std::to_string(targetKnown ? 1 : 0) + "|" +
                          std::to_string(dead ? 1 : 0) + "|" +
                          std::to_string(health) + "|" +
                          std::to_string(armor) + "|" + name;
  if (key == LastKey) {
    setVisible(doc, true);
    return;
  }
  LastKey = key;

  // Health/armor cells disappear when the observer has no target; the mode
  // cell then stretches across the bar (the native panel hides both labels
  // and shows the mode alone).
  setDisplay("spec-health", targetKnown ? "flex" : "none");
  setDisplay("spec-armor", (targetKnown && !dead) ? "flex" : "none");
  if (targetKnown) {
    setDisplay("spec-health-label", dead ? "none" : "inline");
    setInner("spec-health-value",
             dead ? escape(translateOr("Spec_Dead", "Dead"))
                  : std::to_string(health));
    setInner("spec-armor-value", std::to_string(armor));
  }

  const char *token = modeToken(mode);
  const std::string modeText =
      token ? translateOr(token, "Spectator") : "Spectator";
  std::string line = escape(modeText);
  if (targetKnown && !name.empty())
    line += " : " + escape(name);
  setInner("spec-mode-value", line);

  setVisible(doc, true);
}

void onSpectate(void *, const void *) {
  updatePanel();
}

int onNativeHudDraw(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiNativeHudEvent *>(payload);
  if (!Api || !event || event->element != ABCUI_NATIVE_HUD_SPECTATOR)
    return 0;
  AbcUiDocument *doc = panelDocument();
  if (!doc || !enabled() || !Api->ui->documentVisible(doc))
    return 0;
  return 1;
}

// ---- panel callbacks ----

void panelDocumentReady(void *) {
  DocumentReady = true;
  LastKey.clear();
  setInner("spec-health-label", escape(translateOr("Spec_Health", "Health")));
  setInner("spec-armor-label", escape(translateOr("Spec_Armor", "Armor")));
  updatePanel();
}

void panelOnData(void *, double) {
  updatePanel();
}

void panelReset(void *) {
  // VidInit/session teardown keeps the document loaded (HudPanel::reset does
  // not recreate it), so only the presentation state resets here.
  LastKey.clear();
  if (AbcUiDocument *doc = panelDocument())
    Api->ui->documentHide(doc);
}

// ---- lifecycle ----

void onModuleLoad() {
  Api->events->subscribe(ABCUI_CHANNEL_HUD_SPECTATE, 0, &onSpectate, nullptr);
  Api->events->subscribeVeto(ABCUI_CHANNEL_HUD_NATIVE_DRAW, 0,
                             &onNativeHudDraw, nullptr);
  Panel.structSize = sizeof(Panel);
  Panel.name = kPanelName;
  Panel.documentPath = kDocumentPath;
  Panel.zIndex = 0;
  Panel.userdata = nullptr;
  Panel.onDocumentReady = &panelDocumentReady;
  Panel.onData = &panelOnData;
  Panel.onRedraw = nullptr;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);
}

void onModuleInit() {
  SpectatorEnabled =
      Api->console->registerCvar(kSpectatorCvar, "1", ABCUI_CVAR_VALUE);
}

void onModuleShutdown() {
  DocumentReady = false;
  LastKey.clear();
}

void onModuleUnload() {
  Api = nullptr;
}

} // namespace

ABCUI_MODULE_EXPORT int ABCUI_CALL
AbcUiModule_Main(uint32_t abiVersion, const AbcUiModuleApi *api,
                 AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api ||
      !AbcUiModule_InitDescriptor(out))
    return 0;
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->panels ||
      !api->events || !api->ui || !api->console || !api->data ||
      !api->players)
    return 0;
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->console->structSize < sizeof(AbcUiConsoleApi) ||
      api->data->structSize < sizeof(AbcUiDataApi) ||
      api->players->structSize < sizeof(AbcUiPlayersApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.spectator";
  out->version = "1.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
