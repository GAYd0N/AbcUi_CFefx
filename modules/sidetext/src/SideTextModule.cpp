// abcui.sidetext - the left-side speed/score readout. Moved from
// src/panels/sidetext (old CSidePanel / SideTextPanel, ABCEnchance sources:
// src/hud/vgui/sidetext.*; old SideTextPanel.res: 0/c-64 anchor, 72x24,
// template_text "${SCORE}").
//
// The template lives in the document text; {SCORE}/{MAXSPEED}/{SPEED}
// placeholders substitute per frame and the result is written as additive
// glyph ink (plain text is the fallback when a character has no glyph).
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};

// Old CSidePanel ctor default (persisted contract - never rename).
constexpr const char *kSideInfo = "hud_sideinfo";

constexpr const char *kPanelName = "sidetext";
constexpr const char *kDocumentPath =
    "mod://abcui.sidetext/hud/rml/sidetext.rml";

AbcUiCvarHandle SideInfo = nullptr;

std::string Template;
std::string LastText;
float MaxSpeed = 0.0f;
bool TemplateLoaded = false;

AbcUiDocument *panelDocument() {
  return Api->ui->panelDocument(kPanelName);
}

std::string elementInnerRml(AbcUiElement *element) {
  const int needed = Api->ui->elementInnerRML(element, nullptr, 0);
  if (needed <= 0)
    return std::string();
  std::string out(static_cast<size_t>(needed) + 1, '\0');
  Api->ui->elementInnerRML(element, out.data(),
                           static_cast<uint32_t>(out.size()));
  out.resize(static_cast<size_t>(needed));
  return out;
}

void replaceAll(std::string &subject, const char *key,
                const std::string &value) {
  const size_t keyLength = strlen(key);
  size_t pos = 0;
  while ((pos = subject.find(key, pos)) != std::string::npos) {
    subject.replace(pos, keyLength, value);
    pos += value.length();
  }
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

void updateVisibility() {
  // Old hud_sideinfo cvar + suit gate; the old panel also hid with the
  // Viewport-wide HIDEALL token.
  AbcUiHudState state{};
  state.structSize = sizeof(state);
  Api->data->hudState(&state);
  const bool enabled =
      !SideInfo || Api->console->cvarValue(SideInfo, 1.0f) > 0.0f;
  setVisible(state.suit && enabled && !(state.hideBits & ABCUI_HIDEBIT_ALL));
}

// ---- panel callbacks ----

void panelDocumentReady(void *) {
  // The template is the document's initial text (old template_text res
  // entry; the shipped default is "${SCORE}" which carries {SCORE}).
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  if (AbcUiElement *message = Api->ui->querySelector(doc, "message")) {
    Template = elementInnerRml(message);
    TemplateLoaded = !Template.empty();
  }
  MaxSpeed = 0.0f;
  LastText.clear();
}

void panelOnData(void *, double) {
  // Old OnThink evaluated the cvar live (hud_sideinfo callback parity).
  updateVisibility();

  AbcUiDocument *doc = panelDocument();
  if (!doc || !Api->ui->documentVisible(doc) || !TemplateLoaded)
    return;

  AbcUiPlayerState player{};
  player.structSize = sizeof(player);
  Api->data->playerState(&player);
  if (player.index == 0)
    return;

  MaxSpeed = std::max(MaxSpeed, player.speed);

  std::string text = Template;
  replaceAll(text, "{SCORE}", std::to_string(player.frags));
  replaceAll(text, "{MAXSPEED}", std::to_string(static_cast<int>(MaxSpeed)));
  replaceAll(text, "{SPEED}", std::to_string(static_cast<int>(player.speed)));
  if (text == LastText)
    return;
  LastText = text;

  if (AbcUiElement *message = Api->ui->querySelector(doc, "message")) {
    // Ink-only additive glyphs (no black outline layer - the score sits on
    // the scene like the flashlight percentage); a template with characters
    // outside the glyph set falls back to plain text (escaped: the template
    // is document-owned, but the numbers ride the same set-inner path).
    char ink[1024];
    if (Api->ui->inkMarkup(text.c_str(), nullptr, ink, sizeof(ink)))
      Api->ui->elementSetInnerRML(message, ink);
    else
      Api->ui->elementSetInnerRML(message, text.c_str());
  }
}

void panelReset(void *) {
  // Old Reset: hide + max-speed accumulator back to zero.
  AbcUiDocument *doc = panelDocument();
  if (doc)
    Api->ui->documentHide(doc);
  MaxSpeed = 0.0f;
  LastText.clear();
}

// ---- lifecycle ----

void onModuleLoad() {
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
  SideInfo = Api->console->registerCvar(kSideInfo, "1", ABCUI_CVAR_VALUE);
}

void onModuleShutdown() {
  // The document dies with the context; the template reloads on the next
  // one.
  TemplateLoaded = false;
  Template.clear();
  LastText.clear();
  MaxSpeed = 0.0f;
}

void onModuleUnload() {
  Api = nullptr;
}

} // namespace

ABCUI_MODULE_EXPORT int AbcUiModule_Main(uint32_t abiVersion,
                                         const AbcUiModuleApi *api,
                                         AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api || !out)
    return 0;
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->panels ||
      !api->events || !api->ui || !api->console || !api->data)
    return 0;
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->console->structSize < sizeof(AbcUiConsoleApi) ||
      api->data->structSize < sizeof(AbcUiDataApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.sidetext";
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
