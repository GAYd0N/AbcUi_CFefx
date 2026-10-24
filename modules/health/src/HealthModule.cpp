// abcui.health - health/armor bars with the danger hue shift and the longjump
// indicator. Moved from src/panels/health (old CHealthPanel, ABCEnchance
// sources: src/hud/vgui/health.*).
//
// Visibility rule (old Viewport hudhideCallback + SetSpectate): suit
// possessed, neither of the health/battery hide bits fully covering, and not
// spectating. hud_danger_health / hud_danger_armor keep their old names and
// defaults (persisted contract).
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <algorithm>
#include <cstdio>
#include <format>
#include <string>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};

// Old CHealthPanel ctor defaults (persisted contract - never rename).
constexpr const char *kDangerHealth = "hud_danger_health";
constexpr const char *kDangerArmor = "hud_danger_armor";

constexpr const char *kPanelName = "health";
constexpr const char *kDocumentPath = "mod://abcui.health/hud/rml/health.rml";

// HUD accent amber (255 176 0, base.rcss --abc-hud-accent) and the scheme
// "Red" danger colour (255 0 0). The old scheme's "Default" green was
// retuned to the Half-Life amber; the hue-shift path is unchanged.
constexpr float kAccent[3] = {255.0f, 176.0f, 0.0f};
constexpr float kRed[3] = {255.0f, 0.0f, 0.0f};
// Bar geometry (health.rcss meter): 92dp full width.
constexpr float kBarWidth = 92.0f;

AbcUiCvarHandle DangerHealth = nullptr;
AbcUiCvarHandle DangerArmor = nullptr;

AbcUiDocument *panelDocument() {
  return Api->ui->panelDocument(kPanelName);
}

void setVisible(bool visible) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (visible)
    Api->ui->documentShow(doc);
  else
    Api->ui->documentHide(doc);
}

void setElementProperty(const char *id, const char *name, const char *value) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (AbcUiElement *element = Api->ui->querySelector(doc, id))
    Api->ui->elementSetProperty(element, name, value);
}

void setLongJump(bool longJump) {
  setElementProperty("longjump", "visibility", longJump ? "visible" : "hidden");
}

// Old SetHealthVisible/SetArmorVisible element groups.
void setGroupVisible(const char *const *ids, size_t count, bool visible) {
  for (size_t i = 0; i < count; ++i)
    setElementProperty(ids[i], "visibility", visible ? "visible" : "hidden");
}

void updateVisibility() {
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  const bool hideHealth = (state.hideBits & ABCUI_HIDEBIT_HEALTH) != 0;
  const bool hideBattery = (state.hideBits & ABCUI_HIDEBIT_BATTERY) != 0;
  // kHideAll = the old Viewport-wide SetVisible(false) (hud hide-all token
  // from scripted sequences); the group split stays independent of it.
  const bool visible = state.suit && (!hideBattery || !hideHealth) &&
                       !state.spectate && !(state.hideBits & ABCUI_HIDEBIT_ALL);
  setVisible(visible);

  static const char *const kHealthGroup[] = {"health-icon", "health-label",
                                             "health-bar-bg", "health-bar"};
  static const char *const kArmorGroup[] = {"armor-icon", "armor-label",
                                            "armor-bar-bg", "armor-bar"};
  setGroupVisible(kHealthGroup, 4, !hideHealth);
  setGroupVisible(kArmorGroup, 4, !hideBattery);
}

std::string dangerColor(int value, AbcUiCvarHandle cvar, float fallback) {
  const float dangerValue =
      cvar ? Api->console->cvarValue(cvar, fallback) : fallback;
  char color[64];
  if (dangerValue > 0.0f && value < static_cast<int>(dangerValue)) {
    // Old GetDifferColor: hue pulled toward the danger red as the value falls
    // below the threshold (ratio = value / danger).
    Api->ui->differColor(kAccent, kRed, static_cast<float>(value) / dangerValue,
                         color, sizeof(color));
  } else {
    Api->ui->formatColor(kAccent, color, sizeof(color));
  }
  return color;
}

void applyVital(const char *labelId, const char *barId, const char *iconId,
                int value, AbcUiCvarHandle cvar, float fallback) {
  const std::string color = dangerColor(value, cvar, fallback);
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (AbcUiElement *label = Api->ui->querySelector(doc, labelId)) {
    // Plain type with the 1px outline from health.rcss: the additive glyph
    // pass read worse here, so the value is text again.
    const std::string text = std::to_string(value);
    Api->ui->elementSetInnerRML(label, text.c_str());
    Api->ui->elementSetProperty(label, "color", color.c_str());
  }
  // The meter is a flat rounded fill scaled by the value; the icon stays the
  // additive sprite. The danger rule retints both.
  if (AbcUiElement *bar = Api->ui->querySelector(doc, barId)) {
    const float pct = std::clamp(value / 100.0f, 0.0f, 1.0f);
    const std::string width = std::format("{:g}dp", pct * kBarWidth);
    Api->ui->elementSetProperty(bar, "background-color", color.c_str());
    Api->ui->elementSetProperty(bar, "width", width.c_str());
  }
  if (AbcUiElement *icon = Api->ui->querySelector(doc, iconId))
    Api->ui->elementSetProperty(icon, "image-color", color.c_str());
}

void setHealth(int health) {
  // Old g_EventHealth handler: while connected and not spectating, a
  // non-positive health clamps to 1 (spectator feeds report 0).
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  AbcUiPlayerState player{};
  player.structSize = sizeof(player);
  Api->data->playerState(&player);
  if (health <= 0 && player.index != 0 && !state.spectate)
    health = 1;
  applyVital("health-label", "health-bar", "health-icon", health, DangerHealth,
             45.0f);
}

void setArmor(int armor) {
  applyVital("armor-label", "armor-bar", "armor-icon", armor, DangerArmor,
             45.0f);
}

// ---- event sinks ----

void onHealth(void *, const void *payload) {
  setHealth(static_cast<const AbcUiIntEvent *>(payload)->value);
}

void onBattery(void *, const void *payload) {
  setArmor(static_cast<const AbcUiIntEvent *>(payload)->value);
}

void onSuit(void *, const void *) {
  updateVisibility();
}

void onHideToken(void *, const void *) {
  updateVisibility();
}

void onSpectate(void *, const void *) {
  updateVisibility();
}

void onLongJump(void *, const void *payload) {
  setLongJump(static_cast<const AbcUiBoolEvent *>(payload)->value != 0);
}

// ---- panel callbacks ----

void panelDocumentReady(void *) {
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  // Seed from the current snapshot: the change events may have fired before
  // the panels existed (the suit bit arrives on the first client-data frame,
  // ahead of the first HUD_Frame that builds them).
  setHealth(state.health);
  setArmor(state.armor);
  setLongJump(state.longjump != 0);
  updateVisibility();
}

void panelReset(void *) {
  // Old Reset: hide + clear the longjump marker.
  AbcUiDocument *doc = panelDocument();
  if (doc)
    Api->ui->documentHide(doc);
  setLongJump(false);
}

// ---- lifecycle ----

int onNativeHudDraw(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiNativeHudEvent *>(payload);
  return panelDocument() && (event->element == ABCUI_NATIVE_HUD_HEALTH ||
                             event->element == ABCUI_NATIVE_HUD_BATTERY);
}

void onModuleLoad() {
  Api->events->subscribeVeto(ABCUI_CHANNEL_HUD_NATIVE_DRAW, 0, &onNativeHudDraw,
                             nullptr);
  Panel.structSize = sizeof(Panel);
  Panel.name = kPanelName;
  Panel.documentPath = kDocumentPath;
  Panel.zIndex = 0;
  Panel.userdata = nullptr;
  Panel.onDocumentReady = &panelDocumentReady;
  Panel.onData = nullptr;
  Panel.onRedraw = nullptr;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);

  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_HEALTH, 0, &onHealth, nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_NETMSG_BATTERY, 0, &onBattery, nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_SUIT, 0, &onSuit, nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_HIDE_TOKEN, 0, &onHideToken,
                         nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_SPECTATE, 0, &onSpectate, nullptr);
  Api->events->subscribe(ABCUI_CHANNEL_HUD_LONG_JUMP, 0, &onLongJump, nullptr);
}

void onModuleInit() {
  DangerHealth =
      Api->console->registerCvar(kDangerHealth, "45", ABCUI_CVAR_VALUE);
  DangerArmor =
      Api->console->registerCvar(kDangerArmor, "45", ABCUI_CVAR_VALUE);
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
      !api->events || !api->ui || !api->console || !api->data)
    return 0;
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->console->structSize < sizeof(AbcUiConsoleApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.health";
  out->version = "2.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = nullptr;
  out->onUnload = &onModuleUnload;
  return 1;
}
